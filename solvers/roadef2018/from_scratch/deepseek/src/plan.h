// ============================================================================
//  Plan model, independent geometry validator and official solution writer.
//
//  The tree that is emitted for one plate (CUT equals depth, verified on the
//  official A0 example):
//
//    depth 0  plate root  (W x H)
//    depth 1  strips      vertical, widths in [min1Cut, max1Cut], tiling [0, W)
//    depth 2  rows        horizontal inside a strip, sharing its width and x
//    depth 3  items       side by side inside a row, sharing its y and height
//    depth 3  waste       the sliver at the right end of a row
//    depth 2  waste       the band at the top of a strip, or an all-waste strip
//    depth 1  residual    the rightmost strip of the last plate
// ============================================================================
#pragma once

#include "model.h"

namespace ro2018 {

struct PItem { int item = -1; int x = 0, y = 0, w = 0, h = 0; };

// A horizontal row of a strip.  Its height is at least the height of every item
// it holds: an item shorter than the row sits at the bottom of its own column and
// the space above it is a waste leaf of that column (a trim cut).
struct Row {
    int y = 0;          // absolute within the plate
    int h = 0;          // row height, >= every item height in the row
    int usedW = 0;      // width covered by the item columns
    bool waste = false; // true when the row is a waste band (covers defects)
    std::vector<PItem> items;
};

// A vertical strip of the plate: full height, fixed width.
struct Strip {
    int x = 0;
    int w = 0;
    bool residual = false;
    std::vector<Row> rows;   // bottom to top
    int usedH = 0;           // rows bottom -> top extent
};

struct PlatePlan {
    std::vector<Strip> strips;
    int residualW = 0;
    int pendingResidual = 0;   // width kept free at the right edge for the residual
};

// ---------------------------------------------------------------- validation
struct ValidateResult {
    bool ok = false;
    std::string err;
};

inline bool itemFitsItem(const Instance& inst, const PItem& pi) {
    if (pi.item < 0 || pi.item >= (int)inst.items.size()) return false;
    const Item& it = inst.items[pi.item];
    return (pi.w == it.wid && pi.h == it.len) || (pi.w == it.len && pi.h == it.wid);
}

// every defect of the plate must sit inside exactly one waste leaf
inline bool defectsCovered(const Instance& inst, const PlatePlan& P, int plate) {
    const Params& par = inst.par;
    const std::vector<Defect>& defs = inst.defs[plate];
    for (const Defect& d : defs) {
        bool covered = false;
        // strip level leaves: residual and all-waste strips
        for (const Strip& st : P.strips) {
            if (st.residual || st.rows.empty()) {
                if (st.x <= d.x && d.x + d.w <= st.x + st.w) { covered = true; break; }
            }
        }
        if (covered) continue;
        for (const Strip& st : P.strips) {
            if (st.residual || st.rows.empty()) continue;
            int y = 0;
            for (const Row& r : st.rows) {
                // the waste above an item that is shorter than its row
                for (const PItem& pi : r.items) {
                    if (pi.h >= r.h) continue;
                    if (pi.x <= d.x && d.x + d.w <= pi.x + pi.w && pi.y + pi.h <= d.y &&
                        d.y + d.h <= r.y + r.h) {
                        covered = true;
                        break;
                    }
                }
                if (covered) break;
                // right sliver of the row
                if (r.usedW < st.w) {
                    const int gx = st.x + r.usedW, gw = st.w - r.usedW;
                    if (gx <= d.x && d.x + d.w <= gx + gw && r.y <= d.y && d.y + d.h <= r.y + r.h) {
                        covered = true;
                        break;
                    }
                }
                y = r.y + r.h;
            }
            if (covered) break;
            // top band of the strip
            const int gap = par.H - y;
            if (gap > 0 && st.x <= d.x && d.x + d.w <= st.x + st.w && y <= d.y &&
                d.y + d.h <= par.H) {
                covered = true;
                break;
            }
        }
        if (!covered) return false;
    }
    return true;
}

inline ValidateResult validatePlan(const Instance& inst, const std::vector<PlatePlan>& plates) {
    const Params& par = inst.par;
    ValidateResult R;
    if (plates.empty()) { R.err = "no plates"; return R; }
    if ((int)plates.size() > par.nPlates) { R.err = "plate limit exceeded"; return R; }
    std::vector<int> count(inst.items.size(), 0);

    for (size_t p = 0; p < plates.size(); ++p) {
        const PlatePlan& P = plates[p];
        int x = 0;
        for (const Strip& st : P.strips) {
            if (st.x != x) { R.err = "strips do not tile the plate width"; return R; }
            if (st.w <= 0) { R.err = "non positive strip"; return R; }
            if (st.residual) {
                if (!st.rows.empty()) { R.err = "residual carries rows"; return R; }
                if (p + 1 != plates.size()) { R.err = "residual on a non final plate"; return R; }
            } else if (st.rows.empty()) {
                if (st.w < par.minWaste) { R.err = "all waste strip too narrow"; return R; }
            } else {
                if (st.w < par.min1Cut) { R.err = "strip narrower than min1Cut"; return R; }
                if (st.w > par.max1Cut) { R.err = "strip wider than max1Cut"; return R; }
                int y = 0;
                for (const Row& r : st.rows) {
                    if (r.y != y) { R.err = "rows are not contiguous in the strip"; return R; }
                    if (r.waste) {
                        // a waste leaf only needs minWaste in both dimensions: the
                        // checker applies min1Cut to branch nodes alone (the
                        // official A0 example has a 53 mm tall waste row)
                        if (!r.items.empty()) {
                            R.err = "waste row carries " + std::to_string(r.items.size()) + " items";
                            return R;
                        }
                        if (r.h < par.minWaste) { R.err = "waste row thinner than minWaste"; return R; }
                        y += r.h;
                        continue;
                    }
                    if (r.h < par.min1Cut) {
                        R.err = "content row shorter than min1Cut: h=" + std::to_string(r.h);
                        return R;
                    }
                    if (r.items.empty()) { R.err = "empty row"; return R; }
                    int rx = st.x;
                    for (const PItem& pi : r.items) {
                        if (pi.x != rx) { R.err = "row items are not contiguous"; return R; }
                        if (pi.y != r.y) { R.err = "item is not at the row's bottom"; return R; }
                        if (pi.h > r.h) { R.err = "item taller than its row"; return R; }
                        if (pi.h < r.h) {
                            // trim cut: the space above the item is its own waste leaf
                            const int trim = r.h - pi.h;
                            if (trim < par.minWaste) { R.err = "trim waste too thin"; return R; }
                        }
                        if (pi.w <= 0) { R.err = "non positive item"; return R; }
                        if (!itemFitsItem(inst, pi)) { R.err = "item geometry changed"; return R; }
                        if (rectHitsDefects(inst.defs[p], pi.x, pi.y, pi.w, pi.h)) {
                            R.err = "item overlaps a defect";
                            return R;
                        }
                        ++count[pi.item];
                        rx += pi.w;
                    }
                    if (rx - st.x != r.usedW) { R.err = "row width bookkeeping"; return R; }
                    if (r.usedW > st.w) { R.err = "row items exceed the strip width"; return R; }
                    const int gap = st.w - r.usedW;
                    if (gap > 0 && gap < par.minWaste) { R.err = "row right sliver too thin"; return R; }
                    y += r.h;
                }
                if (y > par.H) { R.err = "strip content taller than the plate"; return R; }
                const int gap = par.H - y;
                if (gap > 0 && gap < par.minWaste) { R.err = "strip top band too thin"; return R; }
            }
            x += st.w;
        }
        if (x != par.W) { R.err = "strips do not span the plate width"; return R; }
        if (!defectsCovered(inst, P, (int)p)) { R.err = "a defect is not covered by any leaf"; return R; }
    }
    for (size_t i = 0; i < count.size(); ++i)
        if (count[i] != 1) {
            R.err = "item " + std::to_string(inst.items[i].id) + " placed " +
                    std::to_string(count[i]) + " time(s)";
            return R;
        }
    if (plates.back().residualW <= 0) { R.err = "last plate has no residual"; return R; }
    R.ok = true;
    return R;
}

// ---------------------------------------------------------------- writer
struct EmitStats {
    int plates = 0, residualW = 0, nodes = 0;
    long long waste = 0;
    double wastePct = 0.0;
};

inline bool writeSolution(const Instance& inst, const std::vector<PlatePlan>& plates,
                          const std::string& path, EmitStats& es) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    std::string buf;
    buf.reserve(1 << 20);
    char line[192];
    auto emit = [&](int plate, int nodeId, int x, int y, int w, int h, int type, int cut,
                    int parent) {
        if (parent < 0)
            std::snprintf(line, sizeof line, "%d;%d;%d;%d;%d;%d;%d;%d;\n", plate, nodeId, x, y, w,
                          h, type, cut);
        else
            std::snprintf(line, sizeof line, "%d;%d;%d;%d;%d;%d;%d;%d;%d\n", plate, nodeId, x, y, w,
                          h, type, cut, parent);
        buf += line;
    };
    buf += "PLATE_ID;NODE_ID;X;Y;WIDTH;HEIGHT;TYPE;CUT;PARENT\n";
    int id = 0;
    for (size_t p = 0; p < plates.size(); ++p) {
        const PlatePlan& P = plates[p];
        const int root = id++;
        emit((int)p, root, 0, 0, inst.par.W, inst.par.H, -2, 0, -1);
        for (const Strip& st : P.strips) {
            const int stripId = id++;
            if (st.residual) {
                emit((int)p, stripId, st.x, 0, st.w, inst.par.H, -3, 1, root);
                continue;
            }
            if (st.rows.empty()) {
                emit((int)p, stripId, st.x, 0, st.w, inst.par.H, -1, 1, root);
                continue;
            }
            emit((int)p, stripId, st.x, 0, st.w, inst.par.H, -2, 1, root);
            int y = 0;
            for (const Row& r : st.rows) {
                const int gapRight = st.w - r.usedW;
                if (r.items.size() == 1 && r.h == inst.par.H && gapRight == 0) {
                    // a single full-height item can be a 2-cut leaf of the strip
                    const PItem& pi = r.items.front();
                    emit((int)p, id++, pi.x, pi.y, pi.w, pi.h, inst.items[pi.item].id, 2, stripId);
                    y = r.y + r.h;
                    continue;
                }
                if (r.waste) {
                    // a full-width waste band inside the strip is a 2-cut leaf
                    emit((int)p, id++, st.x, r.y, st.w, r.h, -1, 2, stripId);
                    y = r.y + r.h;
                    continue;
                }
                const int rowId = id++;
                emit((int)p, rowId, st.x, r.y, st.w, r.h, -2, 2, stripId);
                for (const PItem& pi : r.items) {
                    if (pi.h == r.h) {
                        emit((int)p, id++, pi.x, pi.y, pi.w, pi.h, inst.items[pi.item].id, 3,
                             rowId);
                    } else {
                        // trim cut: the column is a 3-cut branch whose 4-cut children
                        // are the item and the waste above it
                        const int colId = id++;
                        emit((int)p, colId, pi.x, r.y, pi.w, r.h, -2, 3, rowId);
                        emit((int)p, id++, pi.x, pi.y, pi.w, pi.h, inst.items[pi.item].id, 4,
                             colId);
                        emit((int)p, id++, pi.x, pi.y + pi.h, pi.w, r.h - pi.h, -1, 4, colId);
                    }
                }
                if (gapRight > 0)
                    emit((int)p, id++, st.x + r.usedW, r.y, gapRight, r.h, -1, 3, rowId);
                y = r.y + r.h;
            }
            const int top = inst.par.H - y;
            if (top > 0) emit((int)p, id++, st.x, y, st.w, top, -1, 2, stripId);
        }
        if (buf.size() > (1u << 22)) { out << buf; buf.clear(); }
    }
    out << buf;
    out.close();
    es.nodes = id;
    es.plates = (int)plates.size();
    es.residualW = plates.empty() ? 0 : plates.back().residualW;
    const long long plateArea = (long long)inst.par.W * inst.par.H;
    const long long used =
        plateArea * (long long)plates.size() - (long long)inst.par.H * es.residualW;
    es.waste = used - inst.totalArea;
    es.wastePct = used > 0 ? 100.0 * (double)es.waste / (double)used : 0.0;
    return true;
}

}  // namespace ro2018
