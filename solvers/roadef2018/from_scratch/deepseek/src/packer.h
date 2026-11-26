// ============================================================================
//  Packer: strips of rows filled strictly in the global precedence order.
//
//  A plate is built left to right.  For every strip the packer tries several
//  candidate widths by simulation and keeps the one that absorbs the most item
//  area; the strip then receives a contiguous block of the item order, arranged
//  as horizontal rows.  A row holds items that share one placed height, side by
//  side; the rows of a strip stack bottom to top and must either reach the plate
//  height exactly or leave a legal waste band on top.
//
//  Items are laid out in exactly the order in which the checker reads them (rows
//  bottom to top, items left to right, strips left to right), so the emitted
//  sequence is always a valid merge of the input stacks.
// ============================================================================
#pragma once

#include "plan.h"

#include <atomic>
#include <random>

namespace ro2018 {

struct PackOptions {
    int maxPlates = 1000;
    double jitter = 0.0;      // 0 = deterministic
    int orient = 0;           // 0 = both orientations, 1 = as read, 2 = rotated
    int lookahead = 80;       // items inspected per row / per width choice
    int widthCands = 8;       // strip widths evaluated per strip
    int widthCap = 0;         // content width kept free on every plate
    int minResidual = 40;     // width kept free on the final plate
    int rowMode = 0;          // how candidate rows are scored, see WorkUnit
};

struct PackResult {
    bool complete = false;
    std::vector<PlatePlan> plates;
    int residualW = 0;
    long long waste = 0;
    double wastePct = 0.0;
    int placed = 0;
    int duplicates = 0;   // items placed more than once; must stay 0
};

class Packer {
public:
    Packer(const Instance& inst) : inst_(inst), par_(inst.par) {}

    PackResult pack(const PackOptions& opt, std::mt19937_64& rng) {
        rng_ = &rng;
        PackResult R;
        // `done` and `remaining` are owned by this function alone: buildStrip only
        // reports the rows it managed to lay out, and nothing is marked placed
        // until the strip is actually part of the plate.
        std::vector<char> done(inst_.items.size(), 0);
        int remaining = (int)inst_.order.size();

        for (int p = 0; p < opt.maxPlates && remaining > 0; ++p) {
            plate_ = p;
            const int keep = std::max(0, std::min(opt.widthCap, par_.W - par_.min1Cut));
            const int plateW = par_.W - keep;

            PlatePlan plan;
            int x = 0;
            int guard = 0;
            while (x + par_.min1Cut <= plateW && guard++ < 300) {
                const int space = std::min(plateW - x, par_.max1Cut);
                Strip st;
                st.x = x;
                const bool okStrip = buildStrip(st, space, opt, done);
                if (!okStrip) break;
                if (st.rows.empty()) break;
                bool dup = false;
                for (const Row& r : st.rows)
                    for (const PItem& pi : r.items)
                        if (done[pi.item]) dup = true;
                if (dup) break;                       // never trust a broken layout
                for (const Row& r : st.rows)
                    for (const PItem& pi : r.items) {
                        done[pi.item] = 1;
                        --remaining;
                    }
                x += st.w;
                plan.strips.push_back(std::move(st));
                if (remaining == 0) break;
            }
            if (plan.strips.empty()) {
                std::fprintf(stderr, "[pkloop] plate=%d empty plan, stops\n", p);
                break;
            }

            std::vector<int> dropped;
            // an earlier plate gains nothing from a wide residual, so it only keeps
            // the minimum free strip; if it happens to be the last one, the free
            // strip becomes the residual
            if (!closePlate(plan, x, par_.minWaste, dropped)) {
                std::fprintf(stderr, "[pkloop] plate=%d closePlate failed at x=%d\n", p, x);
                break;
            }
            for (int ii : dropped)
                if (done[ii]) {
                    done[ii] = 0;
                    ++remaining;
                }
            R.plates.push_back(std::move(plan));
            if (remaining == 0) {
                finalizeResidual(R.plates.back(), opt.minResidual);
                break;
            }
        }

        {
            int inPlan = 0;
            std::vector<int> cnt(inst_.items.size(), 0);
            for (const PlatePlan& pl : R.plates)
                for (const Strip& st : pl.strips)
                    for (const Row& r : st.rows)
                        for (const PItem& pi : r.items) { inPlan++; cnt[pi.item]++; }
            int dup = 0;
            for (int c : cnt)
                if (c > 1) dup++;
            R.placed = inPlan;
            R.duplicates = dup;
        }
R.complete = (remaining == 0) && !R.plates.empty() && R.plates.back().residualW > 0;
        if (R.complete) {
            R.residualW = R.plates.back().residualW;
            const long long plateArea = (long long)par_.W * par_.H;
            const long long used =
                plateArea * (long long)R.plates.size() - (long long)par_.H * R.residualW;
            R.waste = used - inst_.totalArea;
            R.wastePct = used > 0 ? 100.0 * (double)R.waste / (double)used : 0.0;
        }
        return R;
    }

private:
    const Instance& inst_;
    const Params& par_;
    std::mt19937_64* rng_ = nullptr;
    int plate_ = 0;

    double noise() {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        return u(*rng_);
    }

    bool rotAllowed(int rot, const PackOptions& opt) const {
        if (opt.orient == 1) return rot == 0;
        if (opt.orient == 2) return rot == 1;
        return true;
    }

    // An item may be emitted only when every earlier item of its own stack has
    // already been emitted.  This is exactly the checker's sequence rule, so the
    // packer enforces it directly instead of hoping the layout order complies.
    bool isReady(const std::vector<char>& placed, int item) const {
        const std::vector<int>& pos = inst_.stackPos[inst_.items[item].stack];
        for (size_t k = 0; k < pos.size(); ++k) {
            const int ii = inst_.order[pos[k]];
            if (ii == item) return true;
            if (!placed[ii]) return false;
        }
        return false;
    }

    bool cellFree(int x, int y, int w, int h) const {
        return !rectHitsDefects(inst_.defs[plate_], x, y, w, h);
    }

    // Is the band [y, y+h) free of every defect that lies inside [x, x+w)?
    bool bandFree(int x, int w, int y, int h) const {
        for (const Defect& d : inst_.defs[plate_]) {
            if (!(x < d.x + d.w && d.x < x + w)) continue;
            if (y < d.y + d.h && d.y < y + h) return false;
        }
        return true;
    }

    // Smallest legal height of an all-waste band at y covering some defect.
    int wasteBandHeight(int x, int w, int y) const {
        int h = par_.minWaste;
        for (const Defect& d : inst_.defs[plate_]) {
            if (!(x < d.x + d.w && d.x < x + w)) continue;
            if (d.y + d.h <= y) continue;
            if (d.y >= y && d.y + d.h - y > h) h = d.y + d.h - y;
            else if (d.y < y && d.y + d.h > y) h = std::max(h, d.y + d.h - y);
        }
        return h;
    }

    // Finish a plate.  The leftover width at the right edge becomes a plain waste
    // strip; it is turned into the residual later, and only on the last plate of
    // the solution, because the checker allows a residual nowhere else.  When the
    // content runs too close to the right edge the trailing strips are trimmed so
    // that at least `want` stays free.
    bool closePlate(PlatePlan& plan, int x, int want, std::vector<int>& dropped) {
        if (want < par_.minWaste) want = par_.minWaste;
        int left = par_.W - x;
        if (left <= 0) return false;
        if (left < want) {
            for (int k = (int)plan.strips.size() - 1; k >= 0; --k) {
                Strip& st = plan.strips[k];
                const int take = std::min(st.w - par_.min1Cut, want - left);
                if (take <= 0) break;
                st.w -= take;
                left += take;
                // rows inside a narrower strip may now overflow: drop the offending
                // rows and report the items that leave the plan
                while (!st.rows.empty() && st.rows.back().usedW > st.w) {
                    for (const PItem& pi : st.rows.back().items) dropped.push_back(pi.item);
                    st.rows.pop_back();
                }
                int y = 0;
                for (const Row& r : st.rows) y = r.y + r.h;
                if (y > 0 && par_.H - y > 0 && par_.H - y < par_.minWaste) {
                    st.w += take;      // the top band would become illegal: undo
                    left -= take;
                    break;
                }
                st.usedH = y;
                if (left >= want) break;
            }
        }
        if (left < par_.minWaste) return false;
        Strip w;
        w.x = par_.W - left;
        w.w = left;
        plan.strips.push_back(std::move(w));
        plan.pendingResidual = left;
        return true;
    }

    // Turn the rightmost waste strip of the final plate into the residual.
    void finalizeResidual(PlatePlan& plan, int minResidual) {
        if (plan.strips.empty()) return;
        Strip& last = plan.strips.back();
        if (!last.rows.empty()) return;
        if (last.w < std::max(par_.minWaste, minResidual)) return;
        last.residual = true;
        plan.residualW = last.w;
    }

    // Lay out the rows of a strip of width w.  A row is either a content row (a
    // band free of every defect inside the strip, filled with items sharing one
    // placed height) or an all-waste row that covers the defects it spans.  The
    // rows must tile the plate height, because the strip spans the whole plate.
    //
    // The band for a content row is bounded by the next defect bottom inside the
    // strip, so cuts are placed exactly where the checker needs them and the
    // waste row that follows covers the defect completely.
    double layoutStrip(int x, int w, size_t orderPos, const PackOptions& opt,
                       const std::vector<char>& done, std::vector<Row>& rows,
                       std::vector<int>& taken, int& top) {
        rows.clear();
        taken.clear();
        top = 0;
        double area = 0;
        int y = 0;
        int guard = 0;
        // items already laid out inside THIS strip: a strip must never place the
        // same item in two of its rows, which the global `done` set cannot catch
        std::vector<char> placedHere(done);
        while (y < par_.H && guard++ < 300) {
            // Items that may legally be emitted next: for every stack, its first
            // item that is not placed yet.  Any of them can head the next row, so
            // the row height becomes a choice instead of a consequence.
            std::vector<int> heads;
            for (int s = 0; s < inst_.stacks; ++s) {
                const std::vector<int>& pos = inst_.stackPos[s];
                for (size_t k = 0; k < pos.size(); ++k) {
                    const int ii = inst_.order[pos[k]];
                    if (placedHere[ii]) continue;
                    if (isReady(placedHere, ii)) heads.push_back(ii);
                    break;
                }
            }
            if (heads.empty()) break;

            // candidate rows: one per (available item, orientation) that fits here
            struct Cand { int item; int h; int w; };
            std::vector<Cand> cands;
            for (int ii : heads) {
                const Item& it = inst_.items[ii];
                for (int rot = 0; rot < 2; ++rot) {
                    if (!rotAllowed(rot, opt)) continue;
                    const int iw = rot ? it.wid : it.len;
                    const int ih = rot ? it.len : it.wid;
                    if (iw > w) continue;
                    if (y + ih > par_.H) continue;
                    const int gap = par_.H - y - ih;
                    if (gap > 0 && gap < par_.minWaste) continue;
                    if (!cellFree(x, y, iw, ih)) continue;
                    cands.push_back(Cand{ii, ih, iw});
                }
            }
            if (cands.empty()) {
                // Nothing fits at this y.  A defect may be sitting right here, so
                // look for the lowest y below which one of the ready items could
                // start; the gap becomes a waste row that swallows the defect.
                int nextY = -1;
                for (int ii : heads) {
                    const Item& it = inst_.items[ii];
                    for (int rot = 0; rot < 2; ++rot) {
                        if (!rotAllowed(rot, opt)) continue;
                        const int iw = rot ? it.wid : it.len;
                        const int ih = rot ? it.len : it.wid;
                        if (iw > w) continue;
                        // a row can restart right below any defect that reaches into
                        // the current band, which is the only way past a blocked y
                        for (const Defect& d : inst_.defs[plate_]) {
                            if (!(x < d.x + d.w && d.x < x + w)) continue;
                            // the waste row must be at least minWaste tall, which can
                            // be more than the defect's own height
                            const int top = std::max(d.y + d.h, y + par_.minWaste);
                            if (top <= y) continue;
                            if (top + ih > par_.H) continue;
                            const int gap = par_.H - top - ih;
                            if (gap > 0 && gap < par_.minWaste) continue;
                            if (!cellFree(x, top, iw, ih)) continue;
                            if (nextY < 0 || top < nextY) nextY = top;
                        }
                    }
                }
                if (nextY < 0) break;
                Row wrow;
                wrow.y = y;
                wrow.h = nextY - y;
                wrow.waste = true;
                rows.push_back(std::move(wrow));
                y = nextY;
                continue;
            }

            double bestArea = -1;
            Row bestRow;
            std::vector<int> bestTaken;
            for (const Cand& c : cands) {
                const int head = c.item;
                const int h = c.h;
                Row row;
                row.y = y;
                row.h = h;
                row.items.push_back(PItem{head, x, y, c.w, h});
                row.usedW = c.w;
                double a = (double)inst_.items[head].area;
                std::vector<int> rowTaken{head};
                std::vector<char> localPlaced(placedHere);
                localPlaced[head] = 1;
                while (row.usedW < w) {
                    int best = -1, bestW = 0, bestItemH = 0;
                    int seen = 0;
                    for (size_t j = orderPos; j < inst_.order.size(); ++j) {
                        const int ii = inst_.order[j];
                        if (localPlaced[ii]) continue;
                        if (++seen > opt.lookahead) break;
                        if (!isReady(localPlaced, ii)) continue;   // stack precedence
                        const Item& it = inst_.items[ii];
                        for (int rot = 0; rot < 2; ++rot) {
                            if (!rotAllowed(rot, opt)) continue;
                            const int iw = rot ? it.wid : it.len;
                            const int ih = rot ? it.len : it.wid;
                            // a shorter item is allowed: its column gets a trim waste
                            if (ih > h) continue;
                            if (ih < h && h - ih < par_.minWaste) continue;
                            if (row.usedW + iw > w) continue;
                            const int gap = w - (row.usedW + iw);
                            if (gap > 0 && gap < par_.minWaste) continue;
                            if (!cellFree(x + row.usedW, y, iw, ih)) continue;
                            if (iw > bestW) { bestW = iw; bestItemH = ih; best = ii; }
                        }
                    }
                    if (best < 0) break;
                    row.items.push_back(PItem{best, x + row.usedW, y, bestW, bestItemH});
                    row.usedW += bestW;
                    rowTaken.push_back(best);
                    localPlaced[best] = 1;
                    a += (double)inst_.items[best].area;
                }
                const int gapRight = w - row.usedW;
                if (gapRight > 0 && gapRight < par_.minWaste) continue;
                // jitter lets a restart prefer a slightly worse row, which is what
                // keeps the multi-start search from returning the same packing
                double score = a;
                if (opt.rowMode == 1) {
                    score = (double)row.items.size() * 1e7 + a * 1e-3;
                } else if (opt.rowMode == 2) {
                    score = a / (double)std::max(1, row.usedW);
                }
                if (opt.jitter > 0.0) score *= 1.0 + opt.jitter * (noise() - 0.5) * 0.4;
                if (score > bestArea) {
                    bestArea = score;
                    bestRow = row;
                    bestTaken = rowTaken;
                }
            }
            if (bestArea < 0) break;
            const double rowArea = [&] {
                double s = 0;
                for (const PItem& pi : bestRow.items) s += (double)inst_.items[pi.item].area;
                return s;
            }();
            y += bestRow.h;
            rows.push_back(std::move(bestRow));
            for (int ii : bestTaken) {
                taken.push_back(ii);
                placedHere[ii] = 1;
            }
            area += rowArea;
        }
        // when the rows do not reach the plate height the rest is the strip's own
        // waste band, which the emitter writes as the strip's last child
        const int gapTop = par_.H - y;
        if (gapTop > 0 && gapTop < par_.minWaste) return 0;
        top = y;
        return area;
    }

    bool buildStrip(Strip& st, int space, const PackOptions& opt,
                    const std::vector<char>& done) {
        size_t orderPos = 0;
        while (orderPos < inst_.order.size() && done[inst_.order[orderPos]]) ++orderPos;
        if (orderPos >= inst_.order.size()) return false;

        std::vector<int> cands;
        {
            int scanned = 0;
            for (size_t j = orderPos; j < inst_.order.size(); ++j) {
                const int ii = inst_.order[j];
                if (done[ii]) continue;
                if (++scanned > opt.lookahead) break;
                const Item& it = inst_.items[ii];
                for (int rot = 0; rot < 2; ++rot) {
                    if (!rotAllowed(rot, opt)) continue;
                    const int iw = rot ? it.wid : it.len;
                    if (iw >= par_.min1Cut && iw <= space) cands.push_back(iw);
                }
            }
        }
        if (cands.empty()) return false;
        std::sort(cands.begin(), cands.end(), std::greater<int>());
        cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
        if ((int)cands.size() > opt.widthCands) {
            std::vector<int> t;
            const int n = opt.widthCands;
            for (int i = 0; i < n; ++i)
                t.push_back(cands[(size_t)i * (cands.size() - 1) / (size_t)(n - 1)]);
            std::sort(t.begin(), t.end(), std::greater<int>());
            t.erase(std::unique(t.begin(), t.end()), t.end());
            cands.swap(t);
        }

        double bestArea = 0;
        int bestW = -1;
        std::vector<Row> bestRows;
        std::vector<int> bestTaken;
        int bestTop = 0;
        for (int w : cands) {
            if (w < par_.min1Cut || w > space) continue;
            std::vector<Row> rows;
            std::vector<int> taken;
            int top = 0;
            const double area = layoutStrip(st.x, w, orderPos, opt, done, rows, taken, top);
            if (area <= 0 || rows.empty()) continue;
            double score = area;
            if (opt.jitter > 0.0) score += opt.jitter * noise() * 1e6;
            if (score > bestArea) {
                bestArea = score;
                bestW = w;
                bestRows.swap(rows);
                bestTaken.swap(taken);
                bestTop = top;
            }
        }
        if (bestW < 0 || bestRows.empty()) return false;
        st.w = bestW;
        st.rows.swap(bestRows);
        st.usedH = bestTop;
        // tighten the strip to what its rows really cover, when that is legal
        int widest = 0;
        for (const Row& r : st.rows) widest = std::max(widest, r.usedW);
        if (widest >= par_.min1Cut && widest < st.w) {
            bool legal = true;
            for (const Row& r : st.rows) {
                const int gap = widest - r.usedW;
                if (gap > 0 && gap < par_.minWaste) { legal = false; break; }
            }
            if (legal) st.w = widest;
        }
        return true;
    }
};

}  // namespace ro2018
