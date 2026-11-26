// ROADEF2018 glass cutting solver — from-scratch implementation.
//
// Cutting structure: plate -> columns (1-cut, vertical, full height) ->
// shelves (2-cut, horizontal, full column width) -> pieces (3-cut, vertical,
// full shelf height; an item shorter than its shelf gets one 4-cut trim).
// Sequence constraints hold by construction: every placement is append-only,
// so global DFS order equals placement order, and only a stack's next unplaced
// item is ever placed.
//
// Defects: items never overlap defects; every cut line avoids defect
// interiors via jump rules; jump gaps become waste nodes (>= minWaste) that
// fully contain the jumped-over defects.
//
// Emission and validation share one row builder (buildRows), so what is
// validated is exactly what is written.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ----------------------------- data -----------------------------------------

struct Params {
    int nPlates = 100, W = 6000, H = 3210;
    int min1Cut = 100, max1Cut = 3500, min2Cut = 100, minWaste = 20;
};

struct Item { int id, len, wid, stk, seq; };
struct Def { int plate, x, y, w, h; };

static std::vector<std::string> splitSemi(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ';') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

static long toInt(const std::string& s) { return atol(s.c_str()); } // "428.0" -> 428

struct Instance {
    Params P;
    std::vector<Item> items;
    std::vector<std::vector<Def>> defs; // per plate id
    long long itemArea = 0;
    std::string name;
    std::vector<std::vector<int>> stacks; // stack -> item indices in seq order
    int maxStack = 0;
};

static bool parseInstance(const std::string& prefix, const std::string& paramPath,
                          Instance& inst, std::string& err) {
    FILE* f = fopen(paramPath.c_str(), "r");
    if (!f) { err = "cannot open params file"; return false; }
    char line[8192];
    bool first = true;
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (first) { first = false; continue; }
        auto t = splitSemi(s);
        if (t.size() < 2 || t[0].empty()) continue;
        const std::string& name = t[0];
        int val = (int)toInt(t[1]);
        if (name == "nPlates") inst.P.nPlates = val;
        else if (name == "widthPlates") inst.P.W = val;
        else if (name == "heightPlates") inst.P.H = val;
        else if (name == "min1Cut") inst.P.min1Cut = val;
        else if (name == "max1Cut") inst.P.max1Cut = val;
        else if (name == "min2Cut") inst.P.min2Cut = val;
        else if (name == "minWaste") inst.P.minWaste = val;
    }
    fclose(f);

    f = fopen((prefix + "_batch.csv").c_str(), "r");
    if (!f) { err = "cannot open batch file"; return false; }
    first = true;
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (first) { first = false; continue; }
        auto t = splitSemi(s);
        if (t.size() < 5 || t[0].empty()) continue;
        Item it;
        it.id = (int)toInt(t[0]); it.len = (int)toInt(t[1]); it.wid = (int)toInt(t[2]);
        it.stk = (int)toInt(t[3]); it.seq = (int)toInt(t[4]);
        inst.items.push_back(it);
        if (it.stk + 1 > inst.maxStack) inst.maxStack = it.stk + 1;
    }
    fclose(f);

    inst.defs.assign(inst.P.nPlates, {});
    f = fopen((prefix + "_defects.csv").c_str(), "r");
    if (f) {
        first = true;
        while (fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (first) { first = false; continue; }
            auto t = splitSemi(s);
            if (t.size() < 6 || t[0].empty()) continue;
            Def d;
            d.plate = (int)toInt(t[1]); d.x = (int)toInt(t[2]); d.y = (int)toInt(t[3]);
            d.w = (int)toInt(t[4]); d.h = (int)toInt(t[5]);
            if (d.plate >= 0 && d.plate < (int)inst.defs.size() && d.w > 0 && d.h > 0)
                inst.defs[d.plate].push_back(d);
        }
        fclose(f);
    }
    for (auto& it : inst.items) inst.itemArea += (long long)it.len * it.wid;
    if (inst.items.empty()) { err = "empty batch"; return false; }

    inst.stacks.assign(inst.maxStack, {});
    std::vector<int> order((int)inst.items.size());
    for (size_t i = 0; i < inst.items.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Item& A = inst.items[a]; const Item& B = inst.items[b];
        if (A.stk != B.stk) return A.stk < B.stk;
        return A.seq < B.seq;
    });
    for (int idx : order) inst.stacks[inst.items[idx].stk].push_back(idx);

    size_t slash = prefix.find_last_of("/\\");
    inst.name = slash == std::string::npos ? prefix : prefix.substr(slash + 1);
    return true;
}

// ----------------------------- solution --------------------------------------

struct Piece {
    bool waste = false;
    int item = -1;
    int x = 0, w = 0, h = 0; // item h = item height (<= shelf H); waste spans shelf H
};

struct Shelf {
    int y = 0, H = 0, cur = 0;
    std::vector<Piece> pieces;
};

struct Column {
    int plate = 0, x = 0, W = 0, ycur = 0;
    std::vector<Shelf> shelves;
};

struct Solution {
    const Instance* inst = nullptr;
    std::vector<Column> cols;   // creation order == (plate, x) order
    std::vector<int> plateXcur;
    int nPlatesUsed = 0;
    long long residualW = 0;
    long long usedArea() const {
        return ((long long)nPlatesUsed * inst->P.W - residualW) * inst->P.H;
    }
    long long waste() const { return usedArea() - inst->itemArea; }
    double wastePct() const { return 100.0 * (double)waste() / (double)usedArea(); }
};

// ----------------------------- rows ------------------------------------------

struct Row { int plate, x, y, w, h, type, cut, parent; };

static std::vector<Row> buildRows(const Instance& inst, const Solution& sol) {
    std::vector<Row> rows;
    std::vector<std::vector<int>> byPlate(sol.nPlatesUsed);
    for (int ci = 0; ci < (int)sol.cols.size(); ci++)
        byPlate[sol.cols[ci].plate].push_back(ci);
    auto add = [&](int plate, int x, int y, int w, int h, int type, int cut, int parent) {
        rows.push_back({plate, x, y, w, h, type, cut, parent});
    };
    for (int p = 0; p < sol.nPlatesUsed; p++) {
        int plateNode = (int)rows.size();
        add(p, 0, 0, inst.P.W, inst.P.H, -2, 0, -1);
        int xcur = 0;
        for (int ci : byPlate[p]) {
            const Column& col = sol.cols[ci];
            int colNode = (int)rows.size();
            add(p, col.x, 0, col.W, inst.P.H, -2, 1, plateNode);
            int ycur = 0;
            for (const Shelf& sh : col.shelves) {
                if (sh.y > ycur)
                    add(p, col.x, ycur, col.W, sh.y - ycur, -1, 2, colNode);
                int shNode = (int)rows.size();
                add(p, col.x, sh.y, col.W, sh.H, -2, 2, colNode);
                int pcur = col.x;
                for (const Piece& pc : sh.pieces) {
                    if (pc.x > pcur) // defensive gap
                        add(p, pcur, sh.y, pc.x - pcur, sh.H, -1, 3, shNode);
                    if (pc.waste) {
                        add(p, pc.x, sh.y, pc.w, sh.H, -1, 3, shNode);
                    } else if (pc.h == sh.H) {
                        add(p, pc.x, sh.y, pc.w, pc.h, inst.items[pc.item].id, 3, shNode);
                    } else {
                        int pieceNode = (int)rows.size();
                        add(p, pc.x, sh.y, pc.w, sh.H, -2, 3, shNode);
                        add(p, pc.x, sh.y, pc.w, pc.h, inst.items[pc.item].id, 4, pieceNode);
                        add(p, pc.x, sh.y + pc.h, pc.w, sh.H - pc.h, -1, 4, pieceNode);
                    }
                    pcur = pc.x + pc.w;
                }
                int left = col.x + col.W - pcur;
                if (left > 0)
                    add(p, pcur, sh.y, left, sh.H, -1, 3, shNode);
                ycur = sh.y + sh.H;
            }
            if (ycur < inst.P.H)
                add(p, col.x, ycur, col.W, inst.P.H - ycur, -1, 2, colNode);
            xcur = col.x + col.W;
        }
        int leftover = inst.P.W - xcur;
        if (leftover > 0) {
            if (p == sol.nPlatesUsed - 1) {
                add(p, xcur, 0, leftover, inst.P.H, -3, 1, plateNode);
            } else {
                int rem = leftover, pos = xcur;
                while (rem > 0) {
                    int cw = std::min(rem, inst.P.max1Cut);
                    if (rem - cw > 0 && rem - cw < inst.P.minWaste) cw = rem - inst.P.minWaste;
                    add(p, pos, 0, cw, inst.P.H, -1, 1, plateNode);
                    pos += cw; rem -= cw;
                }
            }
        }
    }
    return rows;
}

static bool writeRows(const std::string& path, const std::vector<Row>& rows) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return false;
    fprintf(f, "PLATE_ID;NODE_ID;X;Y;WIDTH;HEIGHT;TYPE;CUT;PARENT\n");
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        if (r.parent < 0)
            fprintf(f, "%d;%d;%d;%d;%d;%d;%d;%d;\n", r.plate, (int)i, r.x, r.y, r.w, r.h, r.type, r.cut);
        else
            fprintf(f, "%d;%d;%d;%d;%d;%d;%d;%d;%d\n", r.plate, (int)i, r.x, r.y, r.w, r.h, r.type, r.cut, r.parent);
    }
    fclose(f);
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// ----------------------------- validation ------------------------------------
// Checker-faithful validation over rows (mirrors official checker semantics).

static std::string validateRows(const Instance& inst, const std::vector<Row>& rows) {
    const Params& P = inst.P;
    // id -> item index
    std::unordered_map<int, int> byId;
    for (size_t q = 0; q < inst.items.size(); q++) byId[inst.items[q].id] = (int)q;
    // node structure
    std::vector<std::vector<int>> kids(rows.size());
    int nPlates = 0;
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        if (r.cut == 0) {
            if (r.parent >= 0) return "plate row has parent";
            if (r.plate != nPlates) return "plate sequence";
            if (r.w != P.W || r.h != P.H) return "plate size";
            nPlates++;
        } else {
            if (r.parent < 0 || r.parent >= (int)i) return "parent must precede node";
            if (r.cut > 4) return "cut stage > 4";
            if (rows[r.parent].plate != r.plate) return "parent on other plate";
            kids[r.parent].push_back((int)i);
        }
    }
    if (nPlates < 1 || nPlates > P.nPlates) return "plate count";
    // children checks per parent
    int residualCount = 0;
    std::vector<long long> defCovered(nPlates, 0);
    std::vector<int> itemSeq;
    std::vector<char> produced(inst.items.size(), 0);
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        auto& ks = kids[i];
        int pc = rows[i].cut; // parent cut
        // 3-cut parents (pieces) may have at most 2 children; others unlimited
        if (pc == 3 && ks.size() > 2) return "4-cut more than 2 children";
        if (pc == 4 && !ks.empty()) return "5-cut exists";
        int cx = r.x, cy = r.y, cw = r.w, chh = r.h;
        int expX = cx, expY = cy;
        for (int k : ks) {
            const Row& c = rows[k];
            if (c.cut != pc + 1) return "child cut depth";
            if (c.type == -3) {
                if (c.cut != 1) return "residual not 1-cut";
                if (r.plate != nPlates - 1) return "residual not on last plate";
                residualCount++;
            }
            if (c.type == -1) {
                if (c.w < P.minWaste || c.h < P.minWaste) return "waste too small";
            }
            if (c.type == -2) {
                if (c.cut == 1 && (c.w < P.min1Cut || c.w > P.max1Cut)) return "1-cut branch width";
                if (c.cut == 2 && c.h < P.min2Cut) return "2-cut branch height";
            }
            if (pc % 2 == 0) { // vertical cut: children x-chained, full height
                if (c.y != cy || c.h != chh) return "vertical child y/h";
                if (c.x != expX) return "vertical child x chain";
                expX += c.w;
            } else { // horizontal: children y-chained, full width
                if (c.x != cx || c.w != cw) return "horizontal child x/w";
                if (c.y != expY) return "horizontal child y chain";
                expY += c.h;
            }
        }
        if (!ks.empty()) {
            if (pc % 2 == 0) { if (expX != cx + cw) return "vertical tiling"; }
            else { if (expY != cy + chh) return "horizontal tiling"; }
        }
        // residual must be last child of last plate's root
        for (size_t z = 0; z < ks.size(); z++) {
            if (rows[ks[z]].type == -3 && (r.plate != nPlates - 1 || z != ks.size() - 1))
                return "residual placement";
        }
        // leaves: defects
        if (ks.empty()) {
            for (const Def& d : inst.defs[r.plate]) {
                bool ov = d.x < r.x + r.w && r.x < d.x + d.w && d.y < r.y + r.h && r.y < d.y + d.h;
                if (!ov) continue;
                if (r.type >= 0) return "item overlaps defect";
                if (!(r.x <= d.x && d.x + d.w <= r.x + r.w && r.y <= d.y && d.y + d.h <= r.y + r.h))
                    return "defect straddles leaf";
                defCovered[r.plate]++;
            }
        }
        if (r.type >= 0) {
            // item leaf: identity, production, sequence order
            auto itf = byId.find(r.type);
            if (itf == byId.end()) return "unknown item id";
            int idx = itf->second;
            const Item& it = inst.items[idx];
            if (!((r.w == it.wid && r.h == it.len) || (r.w == it.len && r.h == it.wid)))
                return "identity mismatch";
            if (produced[idx]) return "duplicate item";
            produced[idx] = 1;
            itemSeq.push_back(idx);
        }
    }
    if (residualCount > 1) return "multiple residuals";
    for (size_t i = 0; i < produced.size(); i++)
        if (!produced[i]) return "item not produced";
    // sequence: walk itemSeq == DFS order (rows are DFS by construction)
    std::vector<int> ptr(inst.maxStack, 0);
    for (int idx : itemSeq) {
        const Item& it = inst.items[idx];
        if (ptr[it.stk] >= (int)inst.stacks[it.stk].size()) return "sequence overflow";
        if (inst.stacks[it.stk][ptr[it.stk]] != idx) return "sequence violated";
        ptr[it.stk]++;
    }
    // every defect on each used plate must be covered exactly once
    for (int p = 0; p < nPlates; p++)
        if (defCovered[p] != (long long)inst.defs[p].size()) return "defect coverage";
    return "";
}

// ----------------------------- knobs -----------------------------------------

using RNG = std::mt19937_64;

struct Knobs {
    double colOpenPen, plateOpenPen, shelfOpenPen, areaGamma, trimPen, noise;
    double compBonus;     // bonus for exactly completing shelf/column/plate
    double heightBonus;   // bonus for shelf heights shared by many remaining items
    int colWidthPolicy;   // 0 exact-first, 1 remainder-first, 2 nice-first, 3 random, 4 nice-good-remainder
    int selectPolicy;     // 0 area-weighted sampling, 1 best-marginal-cost
    int exactFill;        // 0 off, 1 column-fill, 2 plate-fill, 3 both
    int tailRounds;       // improveTail passes per build (0 = off)
    double shelfPad;      // extra height added to new shelves (trim headroom)
    double softTrail;     // penalty factor on remaining shelf width after placement
    int lookDepth;        // 0 off, 1 = one-step lookahead in best-marginal mode
};

struct KnobsRange {
    double lo, hi;
    static KnobsRange of(int i) {
        static const double T[][2] = {
            {0.01, 0.11}, {0.35, 0.75}, {0.05, 0.35}, {0.5, 2.0},
            {0.7, 1.5}, {0.05, 0.30}, {0.2, 3.2}, {0.0, 1.5},
            {0.0, 400.0}, {0.0, 0.6}
        };
        KnobsRange r; r.lo = T[i][0]; r.hi = T[i][1]; return r;
    }
};

static double* knobPtr(Knobs& k, int i) {
    switch (i) {
        case 0: return &k.colOpenPen;
        case 1: return &k.plateOpenPen;
        case 2: return &k.shelfOpenPen;
        case 3: return &k.areaGamma;
        case 4: return &k.trimPen;
        case 5: return &k.noise;
        case 6: return &k.compBonus;
        case 8: return &k.shelfPad;
        default: return &k.softTrail;
    }
}

static Knobs randomKnobs(RNG& rng) {
    std::uniform_real_distribution<double> U(0.0, 1.0);
    Knobs k;
    k.colOpenPen = 0.01 + 0.10 * U(rng);
    k.plateOpenPen = 0.35 + 0.40 * U(rng);
    k.shelfOpenPen = 0.05 + 0.30 * U(rng);
    k.areaGamma = 0.5 + 1.5 * U(rng);
    k.trimPen = 0.7 + 0.8 * U(rng);
    k.noise = 0.05 + 0.25 * U(rng);
    k.compBonus = 0.2 + 3.0 * U(rng);
    k.heightBonus = U(rng) * 1.5;
    k.colWidthPolicy = rng() % 6;
    k.selectPolicy = rng() % 2;
    k.exactFill = rng() % 4;
    k.shelfPad = (double)(rng() % 9) * 50.0;
    k.softTrail = 0.6 * U(rng);
    k.lookDepth = rng() % 2;
    return k;
}

static Knobs mutateKnobs(const Knobs& base, RNG& rng) {
    std::normal_distribution<double> G(0.0, 0.18);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    Knobs k = base;
    for (int i = 0; i < 10; i++) {
        double* v = knobPtr(k, i);
        KnobsRange r = KnobsRange::of(i);
        if (U(rng) < 0.35) {
            *v += G(rng) * (r.hi - r.lo);
            if (*v < r.lo) *v = r.lo;
            if (*v > r.hi) *v = r.hi;
        }
    }
    if (U(rng) < 0.2) k.colWidthPolicy = rng() % 6;
    if (U(rng) < 0.2) k.selectPolicy = rng() % 2;
    if (U(rng) < 0.2) k.exactFill = rng() % 4;
    if (U(rng) < 0.2) k.lookDepth = rng() % 2;
    return k;
}

// ----------------------------- builder ---------------------------------------

static const int NICE_WIDTHS[12] = {3000, 3500, 2000, 2500, 1500, 1750, 1200, 1000, 750, 600, 500, 400};

struct Placement {
    long long cost = 0;
    int kind = -1; // 0 piece in shelf, 1 new shelf, 2 new column, 3 new plate
    int col = -1, shelf = -1, x = 0, y = 0, W = 0, w = 0, h = 0;
    int H = 0;     // shelf height for kind 1 (0 => h)
};

struct Builder {
    const Instance* inst;
    Solution sol;
    std::vector<int> nextSeqIdx;
    std::vector<int> lastCol, lastShelf; // per stack: DFS position of its last placed item
    int placedCount = 0;

    void init(const Instance* i) {
        inst = i;
        nextSeqIdx.assign(inst->maxStack, 0);
    }

    static bool stradX(const Def& d, int b) { return d.x < b && b < d.x + d.w; }
    static bool stradY(const Def& d, int b) { return d.y < b && b < d.y + d.h; }
    static bool xOv(const Def& d, int x0, int w) { return d.x < x0 + w && x0 < d.x + d.w; }
    static bool yOv(const Def& d, int y0, int h) { return d.y < y0 + h && y0 < d.y + d.h; }

    int placeX(const Column& col, const Shelf& sh, int w, int h) const {
        const auto& defs = inst->defs[col.plate];
        int c = sh.cur;
        for (int guard = 0; guard < 500; guard++) {
            int nc = c; bool trig = false;
            for (const Def& d : defs) {
                if (!xOv(d, col.x, col.W) || !yOv(d, sh.y, sh.H)) continue;
                if (stradX(d, c)) { nc = std::max(nc, d.x + d.w); trig = true; }
                else if (xOv(d, c, w) && yOv(d, sh.y, std::min(h, sh.H))) {
                    nc = std::max(nc, d.x + d.w); trig = true;
                } else if (stradX(d, c + w)) { nc = std::max(nc, c + (d.x + d.w - (c + w))); trig = true; }
            }
            if (!trig) break;
            if (nc - sh.cur > 0 && nc - sh.cur < inst->P.minWaste)
                nc = sh.cur + inst->P.minWaste;
            if (nc <= c) return -1;
            c = nc;
        }
        if (c + w > col.x + col.W) return -1;
        int left = col.x + col.W - (c + w);
        if (left != 0 && left < inst->P.minWaste) return -1;
        return c;
    }

    int remainingWithDim(int h) const {
        int cnt = 0;
        for (int s = 0; s < inst->maxStack; s++) {
            const auto& st = inst->stacks[s];
            for (int j = nextSeqIdx[s]; j < (int)st.size(); j++) {
                const Item& it = inst->items[st[j]];
                if (it.len == h || it.wid == h) cnt++;
            }
        }
        return cnt;
    }

    // final y for a new shelf of height H whose first piece is w x h (h <= H,
    // trimmed when h < H); handles straddles and first-piece rect overlaps.
    int placeYAbs(int plate, int x0, int W, int w, int h, int H, int yStart) const {
        const auto& defs = inst->defs[plate];
        int y = yStart;
        for (int guard = 0; guard < 500; guard++) {
            int ny = y; bool trig = false;
            for (const Def& d : defs) {
                if (!xOv(d, x0, W)) continue;
                if (stradY(d, y)) { ny = std::max(ny, d.y + d.h); trig = true; }
                else if (xOv(d, x0, w) && yOv(d, y, h)) { ny = std::max(ny, d.y + d.h); trig = true; }
                else if (stradY(d, y + h)) { ny = std::max(ny, y + (d.y + d.h - (y + h))); trig = true; }
                else if (stradY(d, y + H)) { ny = std::max(ny, y + (d.y + d.h - (y + H))); trig = true; }
                else if (H > h && stradX(d, x0 + w)) { ny = std::max(ny, d.y + d.h); trig = true; }
            }
            if (!trig) break;
            if (ny - yStart > 0 && ny - yStart < inst->P.minWaste)
                ny = yStart + inst->P.minWaste;
            if (ny <= y) return -1;
            y = ny;
        }
        if (y + H > inst->P.H) return -1;
        int left = inst->P.H - (y + H);
        if (left != 0 && left < inst->P.minWaste) return -1;
        return y;
    }

    int placeY(const Column& col, int w, int h) const {
        return placeYAbs(col.plate, col.x, col.W, w, h, h, col.ycur);
    }

    bool colBoundaryClean(int p, int b) const {
        for (const Def& d : inst->defs[p]) if (stradX(d, b)) return false;
        return true;
    }

    // candidate legal widths at x on plate p whose first item has width w
    void colWidthCandidates(int p, int x, int w, std::vector<int>& out) const {
        int lo = (w >= inst->P.min1Cut) ? w : std::max(inst->P.min1Cut, w + inst->P.minWaste);
        int hi = std::min(inst->P.max1Cut, inst->P.W - x);
        auto legal = [&](int W) {
            if (W < lo || W > hi) return false;
            int trail = W - w;
            if (trail != 0 && trail < inst->P.minWaste) return false;
            int left = inst->P.W - (x + W);
            if (left != 0 && left < inst->P.minWaste) return false;
            return colBoundaryClean(p, x + W);
        };
        // exact first
        if (legal(w)) out.push_back(w);
        // plate remainder
        int rem = inst->P.W - x;
        if (legal(rem)) out.push_back(rem);
        // nice widths (dedup, ordered)
        for (int W : NICE_WIDTHS)
            if (legal(W)) out.push_back(W);
        // first legal width >= lo as fallback
        for (int W = lo; W <= hi; W++)
            if (legal(W)) { out.push_back(W); break; }
    }

    int pickColWidth(int p, int x, int w, int h, int policy, RNG& rng) const {
        std::vector<int> cands;
        colWidthCandidates(p, x, w, cands);
        if (cands.empty()) return -1;
        std::vector<int> u; // dedup, preserve order
        for (int v : cands)
            if (std::find(u.begin(), u.end(), v) == u.end()) u.push_back(v);
        int rem = inst->P.W - x;
        auto find = [&](int val) {
            for (int v : u) if (v == val) return v;
            return -1;
        };
        // candidate positions in cands: [0]=exact? [1]=remainder? then nice..., fallback
        // identify the first nice candidate: it is the first value present in NICE list
        int nice = -1;
        for (int W : NICE_WIDTHS) {
            if (std::find(u.begin(), u.end(), W) != u.end()) { nice = W; break; }
        }
        int exact = find(w), remF = find(rem), fallback = u[0];
        int r = -1;
        switch (policy) {
        case 1: r = remF >= 0 ? remF : (exact >= 0 ? exact : fallback); break;
        case 2: r = nice >= 0 ? nice : (exact >= 0 ? exact : fallback); break;
        case 5: {
            // buddy width: w + width of a remaining same-height item (fits one shelf)
            int lo0 = (w >= inst->P.min1Cut) ? w
                    : std::max(inst->P.min1Cut, w + inst->P.minWaste);
            auto legalW = [&](int W2) {
                if (W2 < lo0 || W2 > inst->P.max1Cut || x + W2 > inst->P.W) return false;
                int trail = W2 - w;
                if (trail != 0 && trail < inst->P.minWaste) return false;
                int left = inst->P.W - (x + W2);
                if (left != 0 && left < inst->P.minWaste) return false;
                return colBoundaryClean(p, x + W2);
            };
            for (int s = 0; s < inst->maxStack; s++) {
                if (nextSeqIdx[s] >= (int)inst->stacks[s].size()) continue;
                const Item& b = inst->items[inst->stacks[s][nextSeqIdx[s]]];
                if (b.len == h || b.wid == h) {
                    int bw = (b.len == h) ? b.wid : b.len;
                    int W2 = w + bw;
                    if (legalW(W2)) { r = W2; return r; }
                }
            }
            r = exact >= 0 ? exact : fallback;
            break;
        }
        case 3: r = u[rng() % u.size()]; break;
        case 4: {
            // prefer a width whose remaining plate width is 0 or comfortably reusable
            int lo0 = (w >= inst->P.min1Cut) ? w
                    : std::max(inst->P.min1Cut, w + inst->P.minWaste);
            int bestW = -1, bestScore = INT_MIN;
            for (int v : u) {
                int left = inst->P.W - (x + v);
                int score;
                if (left == 0) score = 3;
                else if (left >= 2 * inst->P.min1Cut) score = 2;
                else if (left >= inst->P.min1Cut) score = 1;
                else score = 0; // 20..99: hard to reuse
                if (v >= lo0) score += 1;
                if (score > bestScore) { bestScore = score; bestW = v; }
            }
            r = bestW;
            break;
        }
        default: r = exact >= 0 ? exact : (nice >= 0 ? nice : fallback); break;
        }
        return r;
    }

    // check the first piece (w x h at x0..x0+w, y..y+h) inside a new shelf:
    // no defect overlap with the item, piece right cut line clear of defects
    // (kept for reference; superseded by placeYAbs which handles jumps)

    // exact-fill a column vertically using frontier items of exact width W:
    // repeatedly add single-item shelves whose heights sum (with defect gaps) to the
    // remaining height; aborts quietly when infeasible. Returns #items added.
    // apply one placement (no exact-fill hooks); returns success
    bool applyPlacement(int candPos, int itemIdx, const Placement& op,
                        int& newColIdx, int& newShelfIdx) {
        (void)candPos;
        const Item& it = inst->items[itemIdx];
        int stk = it.stk;
        if (op.kind == 0) {
            Shelf& sh = sol.cols[op.col].shelves[op.shelf];
            if (op.x > sh.cur) {
                Piece wp; wp.waste = true; wp.x = sh.cur; wp.w = op.x - sh.cur;
                sh.pieces.push_back(wp);
            }
            Piece p; p.item = itemIdx; p.x = op.x; p.w = op.w; p.h = op.h;
            sh.pieces.push_back(p);
            sh.cur = op.x + op.w;
            newColIdx = op.col; newShelfIdx = op.shelf;
        } else if (op.kind == 1) {
            Column& col = sol.cols[op.col];
            int H = op.H > 0 ? op.H : op.h;
            Shelf sh;
            sh.y = op.y; sh.H = H; sh.cur = col.x;
            Piece p; p.item = itemIdx; p.x = col.x; p.w = op.w; p.h = op.h;
            sh.pieces.push_back(p);
            sh.cur = col.x + op.w;
            col.shelves.push_back(std::move(sh));
            col.ycur = op.y + H;
            newColIdx = op.col; newShelfIdx = (int)sol.cols[op.col].shelves.size() - 1;
        } else {
            Column col;
            int firstY;
            if (op.kind == 2) {
                col.plate = sol.nPlatesUsed - 1;
                firstY = placeYAbs(col.plate, op.x, op.W, op.w, op.h, op.h, 0);
            } else {
                col.plate = sol.nPlatesUsed;
                sol.nPlatesUsed++;
                sol.plateXcur.push_back(0);
                firstY = placeYAbs(col.plate, 0, op.W, op.w, op.h, op.h, 0);
            }
            if (firstY < 0) return false;
            col.x = op.x;
            col.W = op.W;
            Shelf sh;
            sh.y = firstY; sh.H = op.h; sh.cur = col.x;
            Piece p; p.item = itemIdx; p.x = col.x; p.w = op.w; p.h = op.h;
            sh.pieces.push_back(p);
            sh.cur = col.x + op.w;
            col.shelves.push_back(std::move(sh));
            col.ycur = firstY + op.h;
            sol.cols.push_back(std::move(col));
            sol.plateXcur[col.plate] = col.x + col.W;
            newColIdx = (int)sol.cols.size() - 1; newShelfIdx = 0;
        }
        placedCount++;
        nextSeqIdx[stk]++;
        lastCol[stk] = newColIdx;
        lastShelf[stk] = newShelfIdx;
        return true;
    }

    // cheapest best-option cost over a SAMPLE of the frontier (for lookahead)
    long long sampledNextCost(const Knobs& kb, RNG& rng, int sample) const {
        std::vector<int> fr;
        for (int s = 0; s < inst->maxStack; s++)
            if (nextSeqIdx[s] < (int)inst->stacks[s].size())
                fr.push_back(inst->stacks[s][nextSeqIdx[s]]);
        if (fr.empty()) return 0;
        long long best = LLONG_MAX;
        int n = std::min(sample, (int)fr.size());
        for (int k = 0; k < n; k++) {
            int idx = fr[rng() % fr.size()];
            std::vector<Placement> o2;
            genOptsFor(idx, kb, rng, o2);
            for (auto& op : o2) best = std::min(best, op.cost);
        }
        return best == LLONG_MAX ? 400000000LL : best;
    }

    int tryExactFillColumn(int ci, RNG& rng) {
        Column& col = sol.cols[ci];
        int added = 0;
        std::uniform_real_distribution<double> U(0.0, 1.0);
        while (added < 40) {
            int R = inst->P.H - col.ycur;
            if (R < inst->P.min2Cut) break;
            // candidate frontier items with an orientation of exact width col.W and height <= R
            std::vector<std::pair<int, int>> cands; // (height, itemIdx)
            for (int s = 0; s < inst->maxStack; s++) {
                if (nextSeqIdx[s] >= (int)inst->stacks[s].size()) continue;
                if (lastCol[s] > ci) continue; // sequence: stack already beyond this column
                int idx = inst->stacks[s][nextSeqIdx[s]];
                const Item& it = inst->items[idx];
                if (it.wid == col.W && it.len <= R && it.len >= inst->P.min2Cut)
                    cands.push_back({it.len, idx});
                else if (it.len == col.W && it.wid <= R && it.wid >= inst->P.min2Cut)
                    cands.push_back({it.wid, idx});
            }
            if (cands.empty()) break;
            // pick the tallest that still leaves 0 or >= min2Cut+minWaste room,
            // preferring exact completion of R
            std::sort(cands.begin(), cands.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            int pickH = -1, pickIdx = -1;
            for (auto& c : cands) {
                int rest = R - c.first;
                if (rest == 0) { pickH = c.first; pickIdx = c.second; break; }
            }
            if (pickIdx < 0) {
                // no exact single completion: choose tallest leaving reusable room
                for (auto& c : cands) {
                    int rest = R - c.first;
                    if (rest == 0 || rest >= inst->P.min2Cut + inst->P.minWaste) {
                        pickH = c.first; pickIdx = c.second; break;
                    }
                }
            }
            if (pickIdx < 0) break;
            const Item& it = inst->items[pickIdx];
            int stk = it.stk;
            if (lastCol[stk] > ci) break; // re-check
            int y = placeY(col, it.wid == col.W ? it.wid : it.len,
                           pickH);
            if (y < 0) break;
            Shelf sh;
            sh.y = y; sh.H = pickH; sh.cur = col.x;
            Piece pc; pc.item = pickIdx; pc.x = col.x; pc.w = col.W; pc.h = pickH;
            sh.pieces.push_back(pc);
            sh.cur = col.x + col.W;
            col.shelves.push_back(std::move(sh));
            col.ycur = y + pickH;
            placedCount++;
            nextSeqIdx[stk]++;
            lastCol[stk] = ci;
            lastShelf[stk] = (int)sol.cols[ci].shelves.size() - 1;
            stackPos[stk].push_back({ci, (int)sol.cols[ci].shelves.size() - 1});
            added++;
        }
        return added;
    }

    // exact-fill the current plate horizontally with new columns of exact item widths:
    // returns #items added (0 if nothing done)
    int tryExactFillPlate(RNG& rng) {
        if (sol.nPlatesUsed == 0) return 0;
        int p = sol.nPlatesUsed - 1;
        int added = 0;
        while (added < 40) {
            int L = inst->P.W - sol.plateXcur[p];
            if (L < inst->P.min1Cut) break;
            // frontier items that could open a column of exact width w here
            struct C { int w, h, idx; };
            std::vector<C> cands;
            for (int s = 0; s < inst->maxStack; s++) {
                if (nextSeqIdx[s] >= (int)inst->stacks[s].size()) continue;
                int idx = inst->stacks[s][nextSeqIdx[s]];
                const Item& it = inst->items[idx];
                if (it.wid >= inst->P.min1Cut && it.wid <= inst->P.max1Cut && it.len <= inst->P.H)
                    cands.push_back({it.wid, it.len, idx});
                if (it.len != it.wid && it.len >= inst->P.min1Cut && it.len <= inst->P.max1Cut
                        && it.wid <= inst->P.H)
                    cands.push_back({it.len, it.wid, idx});
            }
            if (cands.empty()) break;
            // prefer exact width == L, then widest leaving reusable room
            std::sort(cands.begin(), cands.end(),
                      [](const C& a, const C& b) { return a.w > b.w; });
            const C* pick = nullptr;
            for (const C& c : cands)
                if (c.w == L) { pick = &c; break; }
            if (!pick)
                for (const C& c : cands)
                    if (c.w <= L) { pick = &c; break; }
            if (!pick) break;
            int x = sol.plateXcur[p];
            if (!colBoundaryClean(p, x)) break;
            int firstY = placeYAbs(p, x, pick->w, pick->w, pick->h, pick->h, 0);
            if (firstY < 0) break;
            Column col;
            col.plate = p; col.x = x; col.W = pick->w;
            Shelf sh;
            sh.y = firstY; sh.H = pick->h; sh.cur = col.x;
            Piece pc; pc.item = pick->idx; pc.x = col.x; pc.w = pick->w; pc.h = pick->h;
            sh.pieces.push_back(pc);
            sh.cur = col.x + col.W;
            col.shelves.push_back(std::move(sh));
            col.ycur = firstY + pick->h;
            sol.cols.push_back(std::move(col));
            sol.plateXcur[p] = x + pick->w;
            const Item& it = inst->items[pick->idx];
            placedCount++;
            nextSeqIdx[it.stk]++;
            lastCol[it.stk] = (int)sol.cols.size() - 1;
            lastShelf[it.stk] = 0;
            stackPos[it.stk].push_back({(int)sol.cols.size() - 1, 0});
            added++;
        }
        return added;
    }

    void genOptsFor(int itemIdx, const Knobs& kb, RNG& rng,
                    std::vector<Placement>& opts) const {
        const long long plateArea = (long long)inst->P.W * inst->P.H;

            const Item& it = inst->items[itemIdx];
            int stk = it.stk;
            opts.clear();
            int ws[2] = { it.wid, it.len };
            int hs[2] = { it.len, it.wid };
            for (int o = 0; o < 2; o++) {
                if (o == 1 && it.len == it.wid) break;
                int w = ws[o], h = hs[o];
                if (h > inst->P.H || w > inst->P.max1Cut) continue;
                for (int ci = 0; ci < (int)sol.cols.size(); ci++) {
                    const Column& col = sol.cols[ci];
                    for (int si = 0; si < (int)col.shelves.size(); si++) {
                        const Shelf& sh = col.shelves[si];
                        if (!(ci > lastCol[stk] ||
                              (ci == lastCol[stk] && si >= lastShelf[stk]))) continue;
                        if (sh.cur + inst->P.minWaste > col.x + col.W) continue;
                        if (!(h == sh.H || h <= sh.H - inst->P.minWaste)) continue;
                        int x = placeX(col, sh, w, h);
                        if (x < 0) continue;
                        Placement op;
                        op.kind = 0; op.col = ci; op.shelf = si; op.x = x; op.w = w; op.h = h;
                        op.cost = (long long)(x - sh.cur) * sh.H
                                + (long long)(kb.trimPen * (sh.H - h) * w);
                        int leftAfter = col.x + col.W - (x + w);
                        if (leftAfter == 0)
                            op.cost -= (long long)(kb.compBonus * 1000000);
                        else
                            op.cost += (long long)(kb.softTrail
                                    * (double)leftAfter * sh.H);
                        opts.push_back(op);
                    }
                    if (ci >= lastCol[stk] && col.ycur < inst->P.H - inst->P.min2Cut) {
                        int trail = col.W - w;
                        if (trail == 0 || trail >= inst->P.minWaste) {
                            int rem = inst->P.H - col.ycur;
                            int Hcands[3] = { h, rem, h + (int)kb.shelfPad };
                            int nH = 1;
                            if (rem >= h + inst->P.minWaste && rem >= inst->P.min2Cut)
                                Hcands[nH++] = rem;
                            if (kb.shelfPad > 0 && h + (int)kb.shelfPad <= rem
                                    && h + (int)kb.shelfPad >= h + inst->P.minWaste)
                                Hcands[nH++] = h + (int)kb.shelfPad;
                            for (int hi = 0; hi < nH; hi++) {
                                int H = Hcands[hi];
                                if (H < inst->P.min2Cut || H > rem) continue;
                                if (!(H == h || h <= H - inst->P.minWaste)) continue;
                                int y = placeYAbs(col.plate, col.x, col.W, w, h, H, col.ycur);
                                if (y < 0) continue;
                                Placement op;
                                op.kind = 1; op.col = ci; op.y = y; op.w = w; op.h = h;
                                op.H = H;
                                op.cost = (long long)(y - col.ycur) * col.W
                                        + (long long)(kb.trimPen * (double)(H - h) * w)
                                        + (long long)(kb.shelfOpenPen * 1000000);
                                if (y + H == inst->P.H)
                                    op.cost -= (long long)(kb.compBonus * 1000000);
                                op.cost -= (long long)(kb.heightBonus * 1000.0
                                        * remainingWithDim(h) * h / 100.0);
                                opts.push_back(op);
                            }
                        }
                    }
                }
            }
            for (int o = 0; o < 2; o++) {
                if (o == 1 && it.len == it.wid) break;
                int w = ws[o], h = hs[o];
                if (h > inst->P.H || w > inst->P.max1Cut) continue;
                if (sol.nPlatesUsed > 0) {
                    int p = sol.nPlatesUsed - 1;
                    int x = sol.plateXcur[p];
                    int W = pickColWidth(p, x, w, h, kb.colWidthPolicy, rng);
                    if (W > 0 && placeYAbs(p, x, W, w, h, h, 0) >= 0) {
                        Placement op;
                        op.kind = 2; op.W = W; op.x = x; op.w = w; op.h = h;
                        op.cost = (long long)(kb.colOpenPen * plateArea) + (long long)(W - w) * h;
                        if (x + W == inst->P.W)
                            op.cost -= (long long)(kb.compBonus * 1000000);
                        opts.push_back(op);
                    }
                }
                if (sol.nPlatesUsed < inst->P.nPlates) {
                    int p = sol.nPlatesUsed;
                    int W = pickColWidth(p, 0, w, h, kb.colWidthPolicy, rng);
                    if (W > 0 && placeYAbs(p, 0, W, w, h, h, 0) >= 0) {
                        Placement op;
                        op.kind = 3; op.W = W; op.x = 0; op.w = w; op.h = h;
                        op.cost = (long long)(kb.plateOpenPen * plateArea) + (long long)(W - w) * h;
                        opts.push_back(op);
                    }
                }
            }
    }

    std::vector<std::vector<std::pair<int, int>>> stackPos; // per stack: (col, shelf) per placed item

    void resetState() {
        nextSeqIdx.assign(inst->maxStack, 0);
        lastCol.assign(inst->maxStack, -1);
        lastShelf.assign(inst->maxStack, -1);
        stackPos.assign(inst->maxStack, {});
        sol.inst = inst;
        sol.cols.clear();
        sol.plateXcur.clear();
        sol.nPlatesUsed = 0;
        sol.residualW = 0;
        placedCount = 0;
    }

    bool build(RNG& rng, const Knobs& kb) {
        resetState();
        if (!placeAll(rng, kb)) return false;
        if (sol.nPlatesUsed > 0)
            sol.residualW = inst->P.W - sol.plateXcur[sol.nPlatesUsed - 1];
        return sol.nPlatesUsed > 0 && sol.nPlatesUsed <= inst->P.nPlates;
    }

    // rewind to the state before the last `zonePlates` plates were packed,
    // then re-place everything with fresh knobs; keeps the better result.
    bool improveTail(RNG& rng, const Knobs& kb, int zonePlates) {
        if ((int)sol.nPlatesUsed <= zonePlates) return false;
        int p1 = sol.nPlatesUsed - zonePlates;
        // first column index belonging to the zone
        int zc = (int)sol.cols.size();
        while (zc > 0 && sol.cols[zc - 1].plate >= p1) zc--;
        if (zc == (int)sol.cols.size()) return false;
        Solution keep = sol;
        auto keepPos = stackPos;
        // rewind per-stack state to before zone items
        std::vector<int> nsIdx(inst->maxStack, 0), lc(inst->maxStack, -1), ls(inst->maxStack, -1);
        for (int s = 0; s < inst->maxStack; s++) {
            int k = (int)stackPos[s].size();
            while (k > 0 && stackPos[s][k - 1].first >= zc) k--;
            nsIdx[s] = k;
            if (k > 0) { lc[s] = stackPos[s][k - 1].first; ls[s] = stackPos[s][k - 1].second; }
        }
        sol.cols.resize(zc);
        sol.nPlatesUsed = p1;
        sol.plateXcur.resize(p1);
        nextSeqIdx = nsIdx; lastCol = lc; lastShelf = ls;
        stackPos.assign(inst->maxStack, {});
        for (int s = 0; s < inst->maxStack; s++) {
            stackPos[s].assign(keepPos[s].begin(), keepPos[s].begin() + nsIdx[s]);
        }
        placedCount = 0;
        for (int s = 0; s < inst->maxStack; s++) placedCount += nsIdx[s];
        if (!placeAll(rng, kb)) {
            sol = keep; stackPos = keepPos;
            return false;
        }
        if (sol.nPlatesUsed > 0)
            sol.residualW = inst->P.W - sol.plateXcur[sol.nPlatesUsed - 1];
        bool better = sol.waste() < keep.waste();
        if (!better) { sol = keep; stackPos = keepPos; }
        return better;
    }

    bool placeAll(RNG& rng, const Knobs& kb) {
        int n = (int)inst->items.size();
        std::uniform_real_distribution<double> U(0.0, 1.0);
        const long long plateArea = (long long)inst->P.W * inst->P.H;
        long long guard = 0;
        std::vector<Placement> opts;
        std::vector<int> cand;
        std::vector<double> wts;
        while (placedCount < n) {
            if (++guard > 200LL * (n + 10)) return false;
            cand.clear(); wts.clear();

            for (int s = 0; s < inst->maxStack; s++)
                if (nextSeqIdx[s] < (int)inst->stacks[s].size())
                    cand.push_back(inst->stacks[s][nextSeqIdx[s]]);
            wts.resize(cand.size());
            for (size_t i = 0; i < cand.size(); i++) {
                const Item& it = inst->items[cand[i]];
                double a = (double)it.len * it.wid;
                wts[i] = std::pow(a / 1e6, kb.areaGamma) * (0.5 + U(rng)) + 1e-9;
            }
            // candidate order: weighted sampling without replacement (Efraimidis-Spirakis)
            std::vector<std::pair<double, int>> keyed;
            keyed.reserve(cand.size());
            for (size_t i = 0; i < cand.size(); i++) {
                double u = U(rng);
                if (u < 1e-12) u = 1e-12;
                keyed.push_back({ -std::log(u) / wts[i], (int)i });
            }
            std::sort(keyed.begin(), keyed.end());
            std::vector<int> tryOrder;
            tryOrder.reserve(cand.size());
            for (auto& kv : keyed) tryOrder.push_back(kv.second);
            (void)kb;

            // ---- generate options for one item (frontier, respecting sequence) ----


            int chosen = -1;
            Placement chosenOp;
            if (kb.selectPolicy == 1) {
                // best marginal cost across ALL frontier items (+ optional lookahead)
                long long bestc = LLONG_MAX;
                std::vector<std::pair<long long, std::pair<int, Placement>>> pool;
                // first pass: compute each candidate's best cost
                int lkCount = 0;
                std::vector<long long> bestOf(cand.size(), LLONG_MAX);
                std::vector<std::vector<Placement>> allOpts(cand.size());
                long long preBest = LLONG_MAX;
                for (size_t i = 0; i < cand.size(); i++) {
                    genOptsFor(cand[i], kb, rng, allOpts[i]);
                    if (allOpts[i].empty()) continue;
                    for (auto& op : allOpts[i])
                        op.cost += (long long)(U(rng) * kb.noise * (double)(op.cost + 3000));
                    for (auto& op : allOpts[i]) bestOf[i] = std::min(bestOf[i], op.cost);
                    preBest = std::min(preBest, bestOf[i]);
                }
                for (size_t i = 0; i < cand.size(); i++) {
                    if (allOpts[i].empty()) continue;
                    long long b = bestOf[i];
                    // lookahead only for near-best candidates (max 6)
                    long long bonus = 0;
                    if (kb.lookDepth >= 1 && cand.size() >= 2 && b <= preBest * 2) {
                        if (lkCount < 6) {
                            lkCount++;
                            const Placement* bo = nullptr;
                            for (auto& op : allOpts[i])
                                if (!bo || op.cost < bo->cost) bo = &op;
                            Builder tmp = *this;
                            int nc = -1, ns = -1;
                            if (tmp.applyPlacement((int)i, cand[i], *bo, nc, ns))
                                bonus = tmp.sampledNextCost(kb, rng, 8) / 4;
                            else bonus = 400000000LL;
                        }
                    }
                    for (auto& op : allOpts[i])
                        if (op.cost <= b + std::max<long long>(1, b / 10))
                            pool.push_back({op.cost + bonus, {(int)i, op}});
                    bestc = std::min(bestc, b + bonus);
                }
                if (!pool.empty()) {
                    long long lim = bestc + std::max<long long>(1, bestc / 10);
                    std::vector<int> top;
                    for (size_t i = 0; i < pool.size(); i++)
                        if (pool[i].first <= lim) top.push_back((int)i);
                    int pickI = top[(size_t)(U(rng) * top.size())];
                    chosen = cand[pool[pickI].second.first];
                    chosenOp = pool[pickI].second.second;
                }
            } else {
                for (int tryIdx : tryOrder) {
                    int itemIdx = cand[tryIdx];
                    std::vector<Placement> o2;
                    genOptsFor(itemIdx, kb, rng, o2);
                    if (o2.empty()) continue;
                    for (auto& op : o2)
                        op.cost += (long long)(U(rng) * kb.noise * (double)(op.cost + 3000));
                    long long bestc = o2[0].cost;
                    for (auto& op : o2) bestc = std::min(bestc, op.cost);
                    std::vector<int> top;
                    for (size_t i = 0; i < o2.size(); i++)
                        if (o2[i].cost <= bestc + std::max<long long>(1, bestc / 10)) top.push_back((int)i);
                    int ci = top[(size_t)(U(rng) * top.size())];
                    chosen = itemIdx;
                    chosenOp = o2[ci];
                    break;
                }
            }
            if (chosen < 0) return false;
            int itemIdx = chosen;
            const Item& it = inst->items[itemIdx];
            int stk = it.stk;
            Placement op = chosenOp;

            int newColIdx = -1, newShelfIdx = -1;
            if (op.kind == 0) {
                Shelf& sh = sol.cols[op.col].shelves[op.shelf];
                if (op.x > sh.cur) {
                    Piece wp; wp.waste = true; wp.x = sh.cur; wp.w = op.x - sh.cur;
                    sh.pieces.push_back(wp);
                }
                Piece p; p.item = itemIdx; p.x = op.x; p.w = op.w; p.h = op.h;
                sh.pieces.push_back(p);
                sh.cur = op.x + op.w;
                newColIdx = op.col; newShelfIdx = op.shelf;
            } else if (op.kind == 1) {
                Column& col = sol.cols[op.col];
                int H = op.H > 0 ? op.H : op.h;
                Shelf sh;
                sh.y = op.y; sh.H = H; sh.cur = col.x;
                Piece p; p.item = itemIdx; p.x = col.x; p.w = op.w; p.h = op.h;
                sh.pieces.push_back(p);
                sh.cur = col.x + op.w;
                col.shelves.push_back(std::move(sh));
                col.ycur = op.y + H;
                newColIdx = op.col; newShelfIdx = (int)sol.cols[op.col].shelves.size() - 1;
            } else {
                Column col;
                int firstY;
                if (op.kind == 2) {
                    col.plate = sol.nPlatesUsed - 1;
                    firstY = placeYAbs(col.plate, op.x, op.W, op.w, op.h, op.h, 0);
                } else {
                    col.plate = sol.nPlatesUsed;
                    sol.nPlatesUsed++;
                    sol.plateXcur.push_back(0);
                    firstY = placeYAbs(col.plate, 0, op.W, op.w, op.h, op.h, 0);
                }
                if (firstY < 0) return false;
                col.x = op.x;
                col.W = op.W;
                Shelf sh;
                sh.y = firstY; sh.H = op.h; sh.cur = col.x;
                Piece p; p.item = itemIdx; p.x = col.x; p.w = op.w; p.h = op.h;
                sh.pieces.push_back(p);
                sh.cur = col.x + op.w;
                col.shelves.push_back(std::move(sh));
                col.ycur = firstY + op.h;
                sol.cols.push_back(std::move(col));
                sol.plateXcur[col.plate] = col.x + col.W;
                newColIdx = (int)sol.cols.size() - 1; newShelfIdx = 0;
            }
            placedCount++;
            nextSeqIdx[stk]++;
            lastCol[stk] = newColIdx;
            lastShelf[stk] = newShelfIdx;
            stackPos[stk].push_back({newColIdx, newShelfIdx});
            if (kb.exactFill == 1 || kb.exactFill == 3)
                tryExactFillColumn(newColIdx, rng);
            if ((kb.exactFill == 2 || kb.exactFill == 3) && op.kind >= 2)
                tryExactFillPlate(rng);
        }
        return sol.nPlatesUsed > 0 && sol.nPlatesUsed <= inst->P.nPlates;
    }
};

// ----------------------------- driver ----------------------------------------

struct Driver {
    const Instance* inst;
    std::string outPath;
    long long timeLimitMs;
    uint64_t seedBase = 0;
    Solution best;
    bool haveBest = false;
    std::mutex mtx;
    std::atomic<bool> stopFlag{false};
    std::atomic<long long> attempts{0};
    std::atomic<long long> validAttempts{0};

    void consider(const Solution& s) {
        std::lock_guard<std::mutex> lk(mtx);
        if (haveBest && !(s.waste() < best.waste() ||
                          (s.waste() == best.waste() && s.nPlatesUsed < best.nPlatesUsed)))
            return;
        std::vector<Row> rows = buildRows(*inst, s);
        std::string err = validateRows(*inst, rows);
        if (!err.empty()) {
            fprintf(stderr, "[solver] INTERNAL VALIDATION FAILED: %s (solution rejected)\n",
                    err.c_str());
            if (const char* dbg = getenv("SOLVER_DEBUG_DUMP")) {
                FILE* g = fopen(dbg, "w");
                if (g) {
                    fprintf(g, "PLATE_ID;NODE_ID;X;Y;WIDTH;HEIGHT;TYPE;CUT;PARENT\n");
                    for (size_t i = 0; i < rows.size(); i++) {
                        const Row& r = rows[i];
                        if (r.parent < 0)
                            fprintf(g, "%d;%d;%d;%d;%d;%d;%d;%d;\n", r.plate, (int)i, r.x, r.y, r.w, r.h, r.type, r.cut);
                        else
                            fprintf(g, "%d;%d;%d;%d;%d;%d;%d;%d;%d\n", r.plate, (int)i, r.x, r.y, r.w, r.h, r.type, r.cut, r.parent);
                    }
                    fclose(g);
                }
            }
            return;
        }
        if (!writeRows(outPath, rows)) {
            fprintf(stderr, "[solver] write failed\n");
            return;
        }
        best = s;
        haveBest = true;
    }

    struct Elite { Knobs k; double waste; };

    void worker(uint64_t seed) {
        std::vector<Elite> elite; // worker-local: no sharing, no races
        RNG rng(seed);
        Builder bb;
        bb.init(inst);
        std::uniform_real_distribution<double> U(0.0, 1.0);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);
        while (!stopFlag.load(std::memory_order_relaxed) &&
               std::chrono::steady_clock::now() < deadline) {
            Knobs kb;
            if (elite.empty() || U(rng) < 0.3) {
                kb = randomKnobs(rng);
            } else {
                const Elite& e = elite[rng() % elite.size()];
                kb = mutateKnobs(e.k, rng);
            }
            if (bb.build(rng, kb)) {
                for (int r = 0; r < kb.tailRounds; r++)
                    bb.improveTail(rng, kb, 2 + (int)(rng() % 2));
                attempts++;
                consider(bb.sol);
                validAttempts++;
                double w = bb.sol.wastePct();
                Elite e{kb, w};
                elite.push_back(e);
                if (elite.size() > 8) {
                    // drop worst
                    size_t worst = 0;
                    for (size_t i = 1; i < elite.size(); i++)
                        if (elite[i].waste > elite[worst].waste) worst = i;
                    elite.erase(elite.begin() + worst);
                }
            }
        }
    }

    int run() {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeLimitMs);
        { // immediate first solution
            RNG rng(12345);
            Knobs kb; kb.colOpenPen = 0.05; kb.plateOpenPen = 0.5;
            kb.shelfOpenPen = 0.1; kb.areaGamma = 1.0; kb.trimPen = 1.0; kb.noise = 0.1;
            Builder b;
            b.init(inst);
            if (b.build(rng, kb)) consider(b.sol);
        }
        unsigned hw = std::thread::hardware_concurrency();
        int nthreads = (int)std::min<unsigned>(hw ? hw : 8, 8);
        std::vector<std::thread> th;
        for (int i = 0; i < nthreads; i++)
            th.emplace_back(&Driver::worker, this, 0x9E3779B97F4A7C15ULL * (i + 1) + seedBase);
        while (std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stopFlag = true;
        for (auto& t : th) t.join();
        if (!haveBest) { fprintf(stderr, "[solver] no valid solution found\n"); return 1; }
        fprintf(stderr, "[solver] done: attempts=%lld valid=%lld waste=%.4f%% plates=%d residual=%lld\n",
                attempts.load(), validAttempts.load(), best.wastePct(),
                best.nPlatesUsed, best.residualW);
        return 0;
    }
};

int main(int argc, char** argv) {
    std::string instance, params, output;
    long long tl = 900; long long seed = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--instance") instance = next();
        else if (a == "--params") params = next();
        else if (a == "--output") output = next();
        else if (a == "--time-limit") tl = atoll(next().c_str());
        else if (a == "--seed") seed = atoll(next().c_str());
        else { fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (instance.empty() || params.empty() || output.empty()) {
        fprintf(stderr,
            "usage: solver --instance PREFIX --params FILE --output FILE "
            "[--time-limit SEC] [--seed N]\n");
        return 2;
    }
    Instance inst;
    std::string err;
    if (!parseInstance(instance, params, inst, err)) {
        fprintf(stderr, "parse error: %s\n", err.c_str());
        return 2;
    }
    fprintf(stderr, "[solver] %s: %d items, area %.3f plates, %d stacks\n",
            inst.name.c_str(), (int)inst.items.size(),
            (double)inst.itemArea / ((double)inst.P.W * inst.P.H), inst.maxStack);

    Driver drv;
    drv.inst = &inst;
    drv.outPath = output;
    drv.timeLimitMs = tl > 3 ? tl * 1000 - 3000 : tl * 1000;
    drv.seedBase = (uint64_t)seed * 0x2545F4914F6CDD1DULL + 111;
    return drv.run();
}
