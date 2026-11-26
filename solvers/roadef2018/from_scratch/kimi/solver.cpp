// ROADEF2018 from-scratch solver - v5b
// 3-stage guillotine layout: plate -> vertical strips (1-cut) -> horizontal
// rows (2-cut) -> items/waste (3-cut). Online stack-order respecting greedy
// with defect avoidance, randomized restarts within the time budget.
// v4: widened strip-width candidates (anchor + matching-height fillers,
//     max-width option) and row-height tail-fit bonuses in simulation/build.
// v5b: x-aware row-band placement (prefers defect-free bands), defect-edge
//     strip widths, reject strip widths < anchor width, 4-cut pair cells as
//     fallback (row fill and row starters), width multiples for few stacks.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

struct Item { int id, l, w, stack; };
struct Def  { int x, y, w, h; };

struct Params {
    int nPlates = 100, W = 6000, H = 3210;
    int min1 = 100, max1 = 3500, min2 = 100, minW = 20;
};

struct Instance {
    Params pr;
    vector<Item> items;            // batch order
    vector<vector<int>> stacks;    // stack id -> item indices (batch order)
    vector<vector<Def>> defects;   // plate id -> defects
};

static inline vector<string> splitSemi(const string& s) {
    vector<string> out; string cur;
    for (char c : s) { if (c == ';') { out.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
    out.push_back(cur);
    return out;
}
static inline int toInt(const string& s) { return (int)strtod(s.c_str(), nullptr); }

static bool loadParams(const string& path, Params& p) {
    ifstream in(path);
    if (!in) return false;
    string line; getline(in, line);
    while (getline(in, line)) {
        auto t = splitSemi(line);
        if (t.size() < 2 || t[0].empty()) continue;
        if (t[0] == "nPlates") p.nPlates = toInt(t[1]);
        else if (t[0] == "widthPlates") p.W = toInt(t[1]);
        else if (t[0] == "heightPlates") p.H = toInt(t[1]);
        else if (t[0] == "min1Cut") p.min1 = toInt(t[1]);
        else if (t[0] == "max1Cut") p.max1 = toInt(t[1]);
        else if (t[0] == "min2Cut") p.min2 = toInt(t[1]);
        else if (t[0] == "minWaste") p.minW = toInt(t[1]);
    }
    return true;
}

static bool loadInstance(const string& prefix, const string& paramsPath, Instance& in) {
    if (!loadParams(paramsPath, in.pr)) return false;
    {   ifstream f(prefix + "_batch.csv");
        if (!f) return false;
        string line; getline(f, line);
        while (getline(f, line)) {
            auto t = splitSemi(line);
            if (t.size() < 5 || t[0].empty()) continue;
            Item it{toInt(t[0]), toInt(t[1]), toInt(t[2]), toInt(t[3])};
            if (it.stack >= (int)in.stacks.size()) in.stacks.resize(it.stack + 1);
            in.stacks[it.stack].push_back((int)in.items.size());
            in.items.push_back(it);
        }
    }
    {   ifstream f(prefix + "_defects.csv");
        if (!f) return false;
        string line; getline(f, line);
        while (getline(f, line)) {
            auto t = splitSemi(line);
            if (t.size() < 6 || t[0].empty()) continue;
            int pl = toInt(t[1]);
            Def d{toInt(t[2]), toInt(t[3]), toInt(t[4]), toInt(t[5])};
            if (pl >= (int)in.defects.size()) in.defects.resize(pl + 1);
            in.defects[pl].push_back(d);
        }
    }
    return true;
}

struct SNode {
    int x, y, w, h, type; // -3 residual, -2 branch, -1 waste, >=0 item id
    vector<SNode> ch;
    SNode(int x_, int y_, int w_, int h_, int t_) : x(x_), y(y_), w(w_), h(h_), type(t_) {}
};

struct Solution {
    bool ok = false;
    long long waste = (1LL << 62);
    int nPlates = 0;
    int residualW = 0;
    vector<SNode> plates;
};

struct Cfg {
    int anchorPolicy;   // 0 area, 1 width, 2 height, 3 random, 4 simulated density
    int rowPolicy;      // same set
    int fillPolicy;     // 0 width, 1 area, 2 random
    double pRand;
    int beamStacks = 6;   // use per-plate beam search when nStacks <= this
    int beamW = 24;
};

struct Builder {
    const Instance& in;
    mt19937_64 rng;
    Cfg cfg;
    vector<int> headPos;
    int nItems;
    long long itemArea = 0;

    Builder(const Instance& i, uint64_t seed, const Cfg& c) : in(i), rng(seed), cfg(c) {
        headPos.assign(in.stacks.size(), 0);
        nItems = (int)in.items.size();
        for (auto& it : in.items) itemArea += (long long)it.l * it.w;
        dbg = getenv("SOLVER_DEBUG") != nullptr;
    }

    bool dbg = false;

    vector<int> heads() {
        vector<int> hs;
        for (size_t s = 0; s < in.stacks.size(); s++)
            if (headPos[s] < (int)in.stacks[s].size()) hs.push_back(in.stacks[s][headPos[s]]);
        return hs;
    }

    bool boundaryBadX(const vector<Def>& ds, int b) {
        for (auto& d : ds) if (b > d.x && b < d.x + d.w) return true;
        return false;
    }

    // smallest valid row start >= y0 for a row of height h within strip
    // [x0, x0+Ws). Boundary lines must not cross defects overlapping the
    // strip in x; gaps above/below the row must be 0 or >= minW. Prefers a
    // band free of strip-x-overlapping defects; else smallest valid start.
    int adjustY(const vector<Def>& ds, int y0, int h, int x0, int Ws) {
        const Params& P = in.pr;
        vector<int> cand;
        cand.push_back(y0);
        for (auto& d : ds) {
            if (d.x + d.w <= x0 || d.x >= x0 + Ws) continue;
            cand.push_back(d.y + d.h);
            if (d.y - h >= y0) cand.push_back(d.y - h);
        }
        sort(cand.begin(), cand.end());
        int dirty = -1;
        for (int c : cand) {
            if (c < y0) continue;
            if (c - y0 > 0 && c - y0 < P.minW) continue;
            if (c + h > P.H) continue;
            if (P.H - (c + h) > 0 && P.H - (c + h) < P.minW) continue;
            bool bad = false, inside = false;
            for (auto& d : ds) {
                if (d.x + d.w <= x0 || d.x >= x0 + Ws) continue;
                if (c > d.y && c < d.y + d.h) { bad = true; break; }
                if (c + h > d.y && c + h < d.y + d.h) { bad = true; break; }
                if (d.y >= c && d.y + d.h <= c + h) inside = true;
            }
            if (bad) continue;
            if (!inside) return c;
            if (dirty < 0) dirty = c;
        }
        return dirty;
    }

    // ---- simulation helpers (defect-unaware approximations for lookahead) ----
    // pair-cell option: two heads from different stacks, same width w,
    // heights h1 + h2 == oh (i1 bottom, i2 top). At most one per (w, oh).
    struct PR { int w, oh, i1, i2, h1; };
    void pairOptions(const vector<int>& hp, int maxW, int minOh, int maxOh, vector<PR>& out) {
        unordered_map<int, vector<pair<int,int>>> byW; // w -> [(h, itemIdx)]
        for (size_t s = 0; s < in.stacks.size(); s++) {
            if (hp[s] >= (int)in.stacks[s].size()) continue;
            int i = in.stacks[s][hp[s]];
            const Item& it = in.items[i];
            byW[it.l].push_back({it.w, i});
            if (it.w != it.l) byW[it.w].push_back({it.l, i});
        }
        set<uint64_t> seen;
        for (auto& kv : byW) {
            int w = kv.first;
            if (w > maxW) continue;
            auto& v = kv.second;
            int m = (int)v.size();
            for (int a = 0; a < m; a++)
                for (int b = 0; b < m; b++) {
                    if (a == b) continue;
                    int i1 = v[a].second, i2 = v[b].second;
                    if (i1 == i2) continue;
                    if (in.items[i1].stack == in.items[i2].stack) continue;
                    int oh = v[a].first + v[b].first;
                    if (oh < minOh || oh > maxOh) continue;
                    uint64_t key = ((uint64_t)w << 32) | (uint32_t)oh;
                    if (!seen.insert(key).second) continue;
                    out.push_back({w, oh, i1, i2, v[a].first});
                    if ((int)out.size() >= 150) return;
                }
        }
    }

    // fill a row of height oh starting at cur0, advancing hp locally; returns added area
    long long simRowFillFrom(int oh, int Ws, int cur0, vector<int>& hp) {
        int cur = cur0;
        long long area = 0;
        for (;;) {
            int bj = -1, bow = -1;
            for (size_t s = 0; s < in.stacks.size(); s++) {
                if (hp[s] >= (int)in.stacks[s].size()) continue;
                const Item& it = in.items[in.stacks[s][hp[s]]];
                int ow = -1;
                if (it.l == oh) ow = it.w;
                else if (it.w == oh) ow = it.l;
                if (ow < 0 || cur + ow > Ws) continue;
                int rem = Ws - (cur + ow);
                if (!(rem == 0 || rem >= in.pr.minW)) continue;
                if (ow > bow) { bow = ow; bj = (int)s; }
            }
            if (bj < 0) break;
            cur += bow;
            area += (long long)bow * oh;
            hp[bj]++;
        }
        return area;
    }

    // reachable row-height sums (unbounded over distinct head heights at hp);
    // recomputed per strip-row decision; used as a tail-fit nudge.
    vector<uint64_t> tb;
    void computeTailBits(const vector<int>& hp, int cap) {
        int words = cap / 64 + 1;
        tb.assign(words, 0);
        tb[0] = 1;
        for (size_t s2 = 0; s2 < in.stacks.size(); s2++) {
            if (hp[s2] >= (int)in.stacks[s2].size()) continue;
            const Item& jt = in.items[in.stacks[s2][hp[s2]]];
            int hs[2] = {jt.l, jt.w};
            for (int r = 0; r < 2; r++) {
                int h = hs[r];
                if (r == 1 && jt.l == jt.w) continue;
                if (h <= 0 || h > cap) continue;
                int sh = h / 64, off = h % 64;
                uint64_t carry = 0;
                for (int wI = sh; wI < words; wI++) {
                    uint64_t v = tb[wI - sh];
                    uint64_t nv = tb[wI] | (v << off) | carry;
                    carry = off ? (v >> (64 - off)) : 0;
                    tb[wI] = nv;
                }
            }
        }
    }
    long long tailFit(int rem2, int Ws) {
        if (rem2 == 0) return Ws / 16;
        if ((tb[rem2 / 64] >> (rem2 % 64)) & 1) return Ws / 32;
        return 0;
    }

    // simulate strip rows from height y to plate top (defect-unaware);
    // advances hp in place, returns placed item area.
    long long simStripRows(int y, int Ws, vector<int>& hp) {
        long long placed = 0;
        for (;;) {
            int remH = in.pr.H - y;
            if (remH == 0) break;
            computeTailBits(hp, remH);
            long long bestScore = -1, bestArea = 0;
            int bestOh = -1, bestStack = -1, bestOw = 0, bestI1 = -1, bestI2 = -1;
            for (size_t s = 0; s < in.stacks.size(); s++) {
                if (hp[s] >= (int)in.stacks[s].size()) continue;
                int i = in.stacks[s][hp[s]];
                const Item& it = in.items[i];
                int dims[2] = {it.l, it.w};
                for (int r = 0; r < 2; r++) {
                    int ow = dims[r], oh = dims[r ^ 1];
                    if (r == 1 && it.l == it.w) continue;
                    if (oh < in.pr.min2 || oh > remH) continue;
                    if (remH - oh > 0 && remH - oh < in.pr.minW) continue;
                    if (ow > Ws) continue;
                    if (Ws - ow > 0 && Ws - ow < in.pr.minW) continue;
                    vector<int> hp2 = hp;
                    hp2[s]++;
                    long long area = (long long)ow * oh + simRowFillFrom(oh, Ws, ow, hp2);
                    long long score = area / oh + tailFit(remH - oh, Ws);
                    if (score > bestScore || (score == bestScore && area > bestArea)) {
                        bestScore = score; bestArea = area; bestOh = oh;
                        bestStack = (int)s; bestOw = ow; bestI1 = bestI2 = -1;
                    }
                }
            }
            if (bestOh < 0) {
                // pair-started rows as fallback: two heads of equal width,
                // heights summing to oh (used only when no single fits)
                vector<PR> prs;
                pairOptions(hp, Ws, in.pr.min2, remH, prs);
                for (auto& pr : prs) {
                    int oh = pr.oh, ow = pr.w;
                    if (remH - oh > 0 && remH - oh < in.pr.minW) continue;
                    if (Ws - ow > 0 && Ws - ow < in.pr.minW) continue;
                    vector<int> hp2 = hp;
                    hp2[in.items[pr.i1].stack]++;
                    hp2[in.items[pr.i2].stack]++;
                    long long area = (long long)ow * oh + simRowFillFrom(oh, Ws, ow, hp2);
                    long long score = area / oh + tailFit(remH - oh, Ws);
                    if (score > bestScore || (score == bestScore && area > bestArea)) {
                        bestScore = score; bestArea = area; bestOh = oh;
                        bestStack = -1; bestOw = ow; bestI1 = pr.i1; bestI2 = pr.i2;
                    }
                }
            }
            if (bestOh < 0) break;
            if (bestI1 >= 0) {
                hp[in.items[bestI1].stack]++;
                hp[in.items[bestI2].stack]++;
            } else {
                hp[bestStack]++;
            }
            placed += (long long)bestOw * bestOh;
            placed += simRowFillFrom(bestOh, Ws, bestOw, hp);
            y += bestOh;
        }
        return placed;
    }

    // simulate filling a whole strip of width Ws starting with anchor (ai,aow,aoh);
    // returns placed item area (defect-unaware) and advances hp in place.
    long long simStripHp(int ai, int aow, int aoh, int Ws, vector<int>& hp) {
        long long placed = (long long)aow * aoh;
        hp[in.items[ai].stack]++;
        placed += simRowFillFrom(aoh, Ws, aow, hp);
        placed += simStripRows(aoh, Ws, hp);
        return placed;
    }

    long long simStrip(int ai, int aow, int aoh, int Ws) {
        vector<int> hp = headPos;
        return simStripHp(ai, aow, aoh, Ws, hp);
    }

    // anchor candidates for a strip at x0 given head positions hp (defect-aware on x)
    struct AC { int idx, ow, oh, Ws; };
    vector<AC> anchorCands(const vector<int>& hp, int x0, const vector<Def>& ds) {
        const Params& P = in.pr;
        int remW = P.W - x0;
        vector<AC> acs;
        if (remW < P.min1) return acs;
        for (size_t s = 0; s < in.stacks.size(); s++) {
            if (hp[s] >= (int)in.stacks[s].size()) continue;
            int i = in.stacks[s][hp[s]];
            const Item& it = in.items[i];
            int dims[2] = {it.l, it.w};
            for (int r = 0; r < 2; r++) {
                int ow = dims[r], oh = dims[r ^ 1];
                if (r == 1 && it.l == it.w) continue;
                if (oh > P.H || oh < P.min2) continue;
                int Ws = ow;
                if (Ws < P.min1) {
                    if (P.min1 - ow >= P.minW) Ws = P.min1; else continue;
                }
                // strip-width candidates: anchor width, anchor widened by head
                // items matching the anchor row height, defect-edge widths
                // (strip ends right before a defect), and maximal width
                vector<int> wsList;
                wsList.push_back(Ws);
                {
                    vector<int> fills;
                    for (size_t s2 = 0; s2 < in.stacks.size(); s2++) {
                        if (hp[s2] >= (int)in.stacks[s2].size()) continue;
                        int j = in.stacks[s2][hp[s2]];
                        if (j == i) continue;
                        const Item& jt = in.items[j];
                        int wk = -1;
                        if (jt.l == oh) wk = jt.w;
                        else if (jt.w == oh) wk = jt.l;
                        if (wk > 0) fills.push_back(wk);
                    }
                    sort(fills.rbegin(), fills.rend());
                    fills.erase(unique(fills.begin(), fills.end()), fills.end());
                    if (fills.size() > 3) fills.resize(3);
                    for (int wk : fills) wsList.push_back(Ws + wk);
                    // few-stack instances: also try exact multiples so anchor
                    // rows can hold several same-width items
                    if ((int)in.stacks.size() <= cfg.beamStacks)
                        for (int wk : fills)
                            for (int k = 2; k <= 5; k++) wsList.push_back(Ws + k * wk);
                    for (auto& d : ds) {
                        int wEdge = d.x - x0; // stop the strip at the defect's left edge
                        if (wEdge >= ow && wEdge >= P.min1) wsList.push_back(wEdge);
                    }
                    wsList.push_back(min(remW, P.max1));
                }
                sort(wsList.begin(), wsList.end());
                wsList.erase(unique(wsList.begin(), wsList.end()), wsList.end());
                for (int w2 : wsList) {
                    if (w2 < ow || w2 < P.min1 || w2 > P.max1 || w2 > remW) continue;
                    int rr = remW - w2;
                    if (rr > 0 && rr < P.minW) {
                        if (w2 == Ws && remW <= P.max1) w2 = remW; // widen to flush plate edge
                        else continue;
                    }
                    if (boundaryBadX(ds, x0 + w2)) continue;
                    acs.push_back({i, ow, oh, w2});
                }
            }
        }
        return acs;
    }

    // advance a head-position vector after placing item i
    void hpAdvance(vector<int>& hp, int i) { hp[in.items[i].stack]++; }

    // Beam search over strip sequences for ONE plate (few-stack instances).
    // Returns planned strip list: (anchor item idx, ow, oh, Ws).
    struct StripPlan { int aIdx, aow, aoh, Ws; };
    vector<StripPlan> beamPlate(const vector<Def>& ds, int beamW) {
        struct BS { vector<int> hp; int x0; long long area; int parent; StripPlan sp; };
        vector<vector<BS>> layers;
        layers.push_back({BS{headPos, 0, 0, -1, {-1,0,0,0}}});
        vector<StripPlan> bestPlan;
        long long bestArea = -1;
        for (int depth = 0; depth < 64; depth++) {
            vector<BS>& cur = layers.back();
            vector<BS> nxt;
            for (int bi = 0; bi < (int)cur.size(); bi++) {
                BS& st = cur[bi];
                // terminal check handled when added
                auto acs = anchorCands(st.hp, st.x0, ds);
                for (auto& ac : acs) {
                    vector<int> hp2 = st.hp;
                    long long area = simStripHp(ac.idx, ac.ow, ac.oh, ac.Ws, hp2);
                    BS ns;
                    ns.hp = hp2;
                    ns.x0 = st.x0 + ac.Ws;
                    ns.area = st.area + area;
                    ns.parent = bi;
                    ns.sp = {ac.idx, ac.ow, ac.oh, ac.Ws};
                    nxt.push_back(std::move(ns));
                }
                if (acs.empty() || true) {
                    // terminal candidate: current state ends the plate
                    if (st.area > bestArea) {
                        // reconstruct plan
                        vector<StripPlan> plan;
                        int d = depth, b = bi;
                        while (d >= 0 && layers[d][b].parent >= 0) {
                            plan.push_back(layers[d][b].sp);
                            b = layers[d][b].parent; d--;
                        }
                        reverse(plan.begin(), plan.end());
                        bestPlan = plan;
                        bestArea = st.area;
                    }
                }
            }
            if (nxt.empty()) break;
            // prune: keep top beamW by area (tie: larger x0)
            sort(nxt.begin(), nxt.end(), [](const BS& a, const BS& b){
                if (a.area != b.area) return a.area > b.area;
                return a.x0 > b.x0;
            });
            // dedup by hp+x0 signature
            vector<BS> kept;
            for (auto& s : nxt) {
                bool dup = false;
                for (auto& k : kept) {
                    if (k.x0 == s.x0 && k.hp == s.hp) { dup = true; break; }
                }
                if (!dup) kept.push_back(std::move(s));
                if ((int)kept.size() >= beamW) break;
            }
            if (kept.empty()) break;
            layers.push_back(std::move(kept));
        }
        return bestPlan;
    }

    int pickIdx(const vector<double>& scores) {
        int n = (int)scores.size();
        vector<int> ord(n);
        for (int i = 0; i < n; i++) ord[i] = i;
        sort(ord.begin(), ord.end(), [&](int a, int b){ return scores[a] > scores[b]; });
        if (cfg.pRand > 0 && n > 1) {
            uniform_real_distribution<double> U(0, 1);
            if (U(rng) < cfg.pRand) {
                uniform_int_distribution<int> UI(0, min(n, 3) - 1);
                return ord[UI(rng)];
            }
        }
        return ord[0];
    }

    // Build one content row at (x0,y), width Ws, height oh; first cell is item ai
    // (width aow), or a pair cell (ai bottom height ah1, ai2 top) when ai2 >= 0.
    // Returns false only if nothing was placed (safe to retry with another anchor).
    bool buildRow(SNode& strip, int x0, int y, int Ws, int oh, int ai, int ai2, int ah1, int aow,
                  const vector<pair<int,int>>& clusters, int& placedCnt) {
        const Params& P = in.pr;
        placedCnt = 0;
        SNode row(x0, y, Ws, oh, -2);
        int cur = 0;
        bool pendingAnchor = true;
        auto placeItem = [&](int idx, int ow) {
            row.ch.push_back(SNode(x0 + cur, y, ow, oh, in.items[idx].id));
            cur += ow;
            headPos[in.items[idx].stack]++;
            placedCnt++;
        };
        auto placePair = [&](int i1, int i2, int w, int h1) {
            SNode cell(x0 + cur, y, w, oh, -2);
            cell.ch.push_back(SNode(x0 + cur, y, w, h1, in.items[i1].id));
            cell.ch.push_back(SNode(x0 + cur, y + h1, w, oh - h1, in.items[i2].id));
            row.ch.push_back(std::move(cell));
            cur += w;
            headPos[in.items[i1].stack]++;
            headPos[in.items[i2].stack]++;
            placedCnt += 2;
        };
        for (;;) {
            int limit = Ws; size_t ci = clusters.size();
            for (size_t k = 0; k < clusters.size(); k++)
                if (clusters[k].second > cur) { limit = clusters[k].first; ci = k; break; }
            bool hasCluster = ci < clusters.size();
            if (pendingAnchor) {
                if (cur + aow <= limit) {
                    int rem = Ws - (cur + aow);
                    if (rem == 0 || rem >= P.minW) {
                        if (ai2 >= 0) placePair(ai, ai2, aow, ah1);
                        else placeItem(ai, aow);
                        pendingAnchor = false; continue;
                    }
                    if (dbg) fprintf(stderr, "ROWFAIL rem x0=%d y=%d Ws=%d oh=%d aow=%d cur=%d limit=%d\n",
                                     x0, y, Ws, oh, aow, cur, limit);
                    return false; // would leave (0,minW) trailing; nothing placed yet
                }
                if (hasCluster) {
                    int gapEnd = max(clusters[ci].second, cur + P.minW);
                    if (gapEnd > Ws) {
                        if (dbg) fprintf(stderr, "ROWFAIL gap x0=%d y=%d Ws=%d oh=%d aow=%d cur=%d gapEnd=%d\n",
                                         x0, y, Ws, oh, aow, cur, gapEnd);
                        return false;
                    }
                    if (Ws - gapEnd > 0 && Ws - gapEnd < P.minW) gapEnd = Ws;
                    row.ch.push_back(SNode(x0 + cur, y, gapEnd - cur, oh, -1));
                    cur = gapEnd;
                    continue;
                }
                if (dbg) fprintf(stderr, "ROWFAIL noclu x0=%d y=%d Ws=%d oh=%d aow=%d cur=%d\n",
                                 x0, y, Ws, oh, aow, cur);
                return false; // cannot place anchor (unreachable: aow <= Ws)
            }
            // fill candidates: heads with an orientation of height == oh (single
            // cells) plus pair cells (equal width, heights summing to oh)
            vector<int> hs = heads();
            struct FC { int i1, i2, w, h1; };
            vector<FC> fcs; vector<double> sc;
            for (int i : hs) {
                const Item& it = in.items[i];
                int ow = -1;
                if (it.l == oh) ow = it.w;
                else if (it.w == oh) ow = it.l;
                if (ow < 0) continue;
                if (cur + ow > limit) continue;
                int rem = Ws - (cur + ow);
                if (!(rem == 0 || rem >= P.minW)) continue;
                fcs.push_back({i, -1, ow, 0});
                double s;
                switch (cfg.fillPolicy) {
                    case 0: s = ow; break;
                    case 1: s = (double)ow * oh; break;
                    default: s = uniform_real_distribution<double>(0, 1)(rng); break;
                }
                sc.push_back(s);
            }
            if (fcs.empty()) {
                // pair cells as fallback: equal-width heads, heights sum to oh
                vector<PR> prs;
                pairOptions(headPos, min(limit - cur, Ws - cur), oh, oh, prs);
                for (auto& pr : prs) {
                    if (cur + pr.w > limit) continue;
                    int rem = Ws - (cur + pr.w);
                    if (!(rem == 0 || rem >= P.minW)) continue;
                    fcs.push_back({pr.i1, pr.i2, pr.w, pr.h1});
                    double s;
                    switch (cfg.fillPolicy) {
                        case 0: s = pr.w; break;
                        case 1: s = (double)pr.w * oh; break;
                        default: s = uniform_real_distribution<double>(0, 1)(rng); break;
                    }
                    sc.push_back(s);
                }
            }
            if (fcs.empty()) {
                if (hasCluster) {
                    int gapEnd = max(clusters[ci].second, cur + P.minW);
                    if (gapEnd > Ws) break;
                    if (Ws - gapEnd > 0 && Ws - gapEnd < P.minW) gapEnd = Ws;
                    row.ch.push_back(SNode(x0 + cur, y, gapEnd - cur, oh, -1));
                    cur = gapEnd;
                    continue;
                }
                break;
            }
            int sel = pickIdx(sc);
            if (fcs[sel].i2 >= 0) placePair(fcs[sel].i1, fcs[sel].i2, fcs[sel].w, fcs[sel].h1);
            else placeItem(fcs[sel].i1, fcs[sel].w);
        }
        int trail = Ws - cur;
        if (trail > 0) {
            if (trail < P.minW) return false; // unreachable given rules above
            row.ch.push_back(SNode(x0 + cur, y, trail, oh, -1));
        }
        if (placedCnt == 0) return false;
        strip.ch.push_back(std::move(row));
        return true;
    }

    bool buildStrip(SNode& plate, int x0, int Ws, int ai, int aow, int aoh,
                    const vector<Def>& ds, int& itemsPlaced) {
        const Params& P = in.pr;
        itemsPlaced = 0;
        int y = adjustY(ds, 0, aoh, x0, Ws);
        if (y < 0) return false;
        SNode strip(x0, 0, Ws, P.H, -2);
        auto clustersFor = [&](int ry0, int ry1) {
            vector<pair<int,int>> iv;
            for (auto& d : ds)
                if (d.x >= x0 && d.x + d.w <= x0 + Ws && d.y >= ry0 && d.y + d.h <= ry1)
                    iv.push_back({d.x - x0, d.x + d.w - x0});
            sort(iv.begin(), iv.end());
            vector<pair<int,int>> cl;
            for (auto& pr : iv) {
                if (!cl.empty() && pr.first - cl.back().second < P.minW) cl.back().second = max(cl.back().second, pr.second);
                else cl.push_back(pr);
            }
            return cl;
        };
        if (y > 0) strip.ch.push_back(SNode(x0, 0, Ws, y, -1));
        int cnt = 0;
        if (!buildRow(strip, x0, y, Ws, aoh, ai, -1, 0, aow, clustersFor(y, y + aoh), cnt)) return false;
        itemsPlaced += cnt;
        y += aoh;
        for (;;) {
            int remH = P.H - y;
            if (remH == 0) break;
            computeTailBits(headPos, remH);
            vector<int> hs = heads();
            struct RC { int idx, idx2, h1, ow, oh; };
            vector<RC> rcs; vector<double> sc;
            auto scoreRow = [&](int i1, int i2, int ow, int oh) {
                double s;
                switch (cfg.rowPolicy) {
                    case 0: s = (double)ow * oh; break;
                    case 1: s = oh; break;
                    case 2: {
                        int cnt2 = 0;
                        for (int j : hs) if (in.items[j].l == oh || in.items[j].w == oh) cnt2++;
                        s = (double)oh * (1 + cnt2); break;
                    }
                    case 4: {
                        // simulated density: fill area per unit row height
                        vector<int> hp2 = headPos;
                        hp2[in.items[i1].stack]++;
                        if (i2 >= 0) hp2[in.items[i2].stack]++;
                        long long area = (long long)ow * oh + simRowFillFrom(oh, Ws, ow, hp2);
                        s = (double)area / oh + 1e-9 * (double)area;
                        s += (double)tailFit(remH - oh, Ws);
                        break;
                    }
                    default: s = uniform_real_distribution<double>(0, 1)(rng); break;
                }
                return s;
            };
            for (int i : hs) {
                const Item& it = in.items[i];
                int dims[2] = {it.l, it.w};
                for (int r = 0; r < 2; r++) {
                    int ow = dims[r], oh = dims[r ^ 1];
                    if (r == 1 && it.l == it.w) continue;
                    if (oh < P.min2 || oh > remH) continue;
                    if (remH - oh > 0 && remH - oh < P.minW) continue;
                    if (ow > Ws) continue;
                    if (Ws - ow > 0 && Ws - ow < P.minW) continue;
                    int y0 = adjustY(ds, y, oh, x0, Ws);
                    if (y0 < 0) continue;
                    rcs.push_back({i, -1, 0, ow, oh});
                    sc.push_back(scoreRow(i, -1, ow, oh));
                }
            }
            // pair-started rows: fallback when no single head fits, plus
            // exact-closure pairs (oh == remH) which turn the strip tail
            // into items even when single candidates exist
            {
                vector<PR> prs;
                if (rcs.empty()) pairOptions(headPos, Ws, P.min2, remH, prs);
                else pairOptions(headPos, Ws, remH, remH, prs);
                for (auto& pr : prs) {
                    int oh = pr.oh, ow = pr.w;
                    if (remH - oh > 0 && remH - oh < P.minW) continue;
                    if (Ws - ow > 0 && Ws - ow < P.minW) continue;
                    int y0 = adjustY(ds, y, oh, x0, Ws);
                    if (y0 < 0) continue;
                    rcs.push_back({pr.i1, pr.i2, pr.h1, ow, oh});
                    sc.push_back(scoreRow(pr.i1, pr.i2, ow, oh));
                }
            }
            if (rcs.empty()) break;
            // try row candidates in policy order until one actually builds
            vector<int> rem(rcs.size());
            for (size_t i = 0; i < rcs.size(); i++) rem[i] = i;
            vector<double> scRem = sc;
            bool rowBuilt = false;
            while (!rem.empty()) {
                int sel = pickIdx(scRem);
                RC rc = rcs[rem[sel]];
                rem.erase(rem.begin() + sel);
                scRem.erase(scRem.begin() + sel);
                int y0 = adjustY(ds, y, rc.oh, x0, Ws);
                if (y0 < 0) continue;
                int pc = 0;
                size_t childMark = strip.ch.size();
                if (y0 > y) strip.ch.push_back(SNode(x0, y, Ws, y0 - y, -1));
                if (buildRow(strip, x0, y0, Ws, rc.oh, rc.idx, rc.idx2, rc.h1, rc.ow, clustersFor(y0, y0 + rc.oh), pc)) {
                    itemsPlaced += pc;
                    y = y0 + rc.oh;
                    rowBuilt = true;
                    break;
                }
                strip.ch.erase(strip.ch.begin() + childMark, strip.ch.end()); // rollback waste row
            }
            if (!rowBuilt) break;
        }
        if (P.H - y > 0) strip.ch.push_back(SNode(x0, y, Ws, P.H - y, -1));
        plate.ch.push_back(std::move(strip));
        return true;
    }

    Solution construct() {
        const Params& P = in.pr;
        static const vector<Def> emptyDefs;
        Solution sol;
        int plateIdx = 0;
        int totalPlaced = 0;
        bool beamOn = (int)in.stacks.size() <= cfg.beamStacks;
        while (totalPlaced < nItems) {
            if (plateIdx >= P.nPlates) { sol.ok = false; return sol; }
            const vector<Def>& ds = (plateIdx < (int)in.defects.size()) ? in.defects[plateIdx] : emptyDefs;
            SNode plate(0, 0, P.W, P.H, -2);
            int x0 = 0;
            vector<StripPlan> plan;
            size_t planPos = 0;
            if (beamOn) plan = beamPlate(ds, cfg.beamW);
            for (;;) {
                int remW = P.W - x0;
                if (remW == 0) break;
                if (remW < P.min1) {
                    plate.ch.push_back(SNode(x0, 0, remW, P.H, -1));
                    x0 = P.W;
                    break;
                }
                bool built = false;
                // 1) follow beam plan while it stays consistent with real state
                while (beamOn && planPos < plan.size() && !built) {
                    StripPlan& sp = plan[planPos];
                    int i = sp.aIdx;
                    int stk = in.items[i].stack;
                    bool isHead = headPos[stk] < (int)in.stacks[stk].size() &&
                                  in.stacks[stk][headPos[stk]] == i;
                    if (!isHead) { planPos = plan.size(); break; }
                    int rr = remW - sp.Ws;
                    bool feas = sp.Ws >= P.min1 && sp.Ws <= P.max1 && sp.Ws <= remW &&
                                (rr == 0 || rr >= P.minW) && !boundaryBadX(ds, x0 + sp.Ws);
                    if (!feas) { planPos = plan.size(); break; }
                    int pc = 0;
                    if (buildStrip(plate, x0, sp.Ws, sp.aIdx, sp.aow, sp.aoh, ds, pc)) {
                        x0 += sp.Ws;
                        totalPlaced += pc;
                        built = true;
                        planPos++;
                    } else {
                        planPos = plan.size(); // abandon plan, greedy continues
                    }
                }
                // 2) greedy anchor selection
                if (!built) {
                    vector<AC> acs = anchorCands(headPos, x0, ds);
                    vector<double> sc;
                    for (auto& ac : acs) {
                        double s;
                        switch (cfg.anchorPolicy) {
                            case 0: s = (double)ac.ow * ac.oh; break;
                            case 1: s = ac.ow; break;
                            case 2: s = ac.oh; break;
                            case 4: {
                                long long area = simStrip(ac.idx, ac.ow, ac.oh, ac.Ws);
                                s = (double)area / ac.Ws + 1e-9 * (double)area;
                                break;
                            }
                            default: s = uniform_real_distribution<double>(0, 1)(rng); break;
                        }
                        sc.push_back(s);
                    }
                    if (!acs.empty()) {
                        vector<int> rem(acs.size());
                        for (size_t i = 0; i < acs.size(); i++) rem[i] = i;
                        vector<double> scRem = sc;
                        while (!rem.empty() && !built) {
                            int sel = pickIdx(scRem);
                            AC ac = acs[rem[sel]];
                            rem.erase(rem.begin() + sel);
                            scRem.erase(scRem.begin() + sel);
                            int pc = 0;
                            if (buildStrip(plate, x0, ac.Ws, ac.idx, ac.ow, ac.oh, ds, pc)) {
                                x0 += ac.Ws;
                                totalPlaced += pc;
                                built = true;
                            }
                        }
                    }
                }
                if (!built) {
                    // waste strip fallback: advance past nearest safe boundary
                    if (getenv("SOLVER_DEBUG")) {
                        vector<AC> acsDbg = anchorCands(headPos, x0, ds);
                        int fAdj = 0, fRow = 0;
                        for (auto& ac : acsDbg) {
                            int yy = adjustY(ds, 0, ac.oh, x0, ac.Ws);
                            if (yy < 0) fAdj++;
                        }
                        fprintf(stderr, "FALLBACK x0=%d acs=%zu adjFail=%d\n",
                                x0, acsDbg.size(), fAdj);
                    }
                    vector<int> candB;
                    candB.push_back(x0 + P.minW);
                    for (auto& d : ds) { candB.push_back(d.x); candB.push_back(d.x + d.w); }
                    candB.push_back(P.W);
                    sort(candB.begin(), candB.end());
                    int b = -1;
                    for (int cb : candB) {
                        if (cb - x0 < P.minW) continue;
                        if (P.W - cb > 0 && P.W - cb < P.minW) continue;
                        if (boundaryBadX(ds, cb)) continue;
                        b = cb; break;
                    }
                    if (b < 0) b = P.W;
                    plate.ch.push_back(SNode(x0, 0, b - x0, P.H, -1));
                    x0 = b;
                    if (x0 >= P.W) break;
                }
            }
            // guarantee tiling up to W
            int cov = 0;
            for (auto& c : plate.ch) cov = max(cov, c.x + c.w);
            if (cov < P.W && P.W - cov >= P.minW)
                plate.ch.push_back(SNode(cov, 0, P.W - cov, P.H, -1));
            bool moreItems = totalPlaced < nItems;
            if (!moreItems) {
                if (!plate.ch.empty() && plate.ch.back().type == -1) plate.ch.back().type = -3;
                sol.residualW = 0;
                for (auto& c : plate.ch) if (c.type == -3) sol.residualW = c.w;
                sol.plates.push_back(std::move(plate));
                sol.nPlates = plateIdx + 1;
                sol.ok = true;
                long long used = ((long long)sol.nPlates * P.W - sol.residualW) * P.H;
                sol.waste = used - itemArea;
                return sol;
            }
            sol.plates.push_back(std::move(plate));
            plateIdx++;
        }
        sol.ok = false;
        return sol;
    }
};

static void emitSolution(const Solution& sol, const Params& P, const string& path) {
    string tmp = path + ".tmp";
    ofstream out(tmp);
    out << "PLATE_ID;NODE_ID;X;Y;WIDTH;HEIGHT;TYPE;CUT;PARENT\n";
    int nid = 0;
    for (int p = 0; p < sol.nPlates; p++) {
        const SNode& pl = sol.plates[p];
        int plateNid = nid;
        out << p << ";" << nid++ << ";0;0;" << P.W << ";" << P.H << ";-2;0;\n";
        auto dfs = [&](auto&& self, const SNode& n, int parent, int cut) -> void {
            int my = nid;
            out << p << ";" << nid++ << ";" << n.x << ";" << n.y << ";" << n.w << ";" << n.h
                << ";" << n.type << ";" << cut << ";" << parent << "\n";
            for (auto& c : n.ch) self(self, c, my, cut + 1);
        };
        for (auto& c : pl.ch) dfs(dfs, c, plateNid, 1);
    }
    out.flush();
    out.close();
    rename(tmp.c_str(), path.c_str());
}

int main(int argc, char** argv) {
    string instance, params, output;
    int timeLimit = 850;
    uint64_t seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        string a = argv[i];
        if (a == "--instance") instance = argv[i + 1];
        else if (a == "--params") params = argv[i + 1];
        else if (a == "--output") output = argv[i + 1];
        else if (a == "--time-limit") timeLimit = atoi(argv[i + 1]);
        else if (a == "--seed") seed = strtoull(argv[i + 1], nullptr, 10);
    }
    if (instance.empty() || params.empty() || output.empty()) {
        fprintf(stderr, "usage: --instance P --params F --output F [--time-limit S] [--seed N]\n");
        return 2;
    }
    Instance in;
    if (!loadInstance(instance, params, in)) {
        fprintf(stderr, "failed to load instance\n");
        return 2;
    }
    auto t0 = Clock::now();
    double budget = max(1, timeLimit - 10);
    double stagSec = 60.0;
    if (const char* e = getenv("SOLVER_STAGNANT")) stagSec = atof(e);
    mt19937_64 rng(seed);

    Solution best;
    long long iter = 0;
    double tLastImprove = 0.0;
    int baseAnchor = 4, baseRow = 4; // simulation-density policies (v2 default)
    if (const char* e = getenv("SOLVER_POLICIES")) sscanf(e, "%d,%d", &baseAnchor, &baseRow);
    double biasSim = 0.4; // restart probability of the (4,4) simulation config
    if (const char* e = getenv("SOLVER_BIAS")) biasSim = atof(e);
    int beamStacks = 6, beamW = 24;
    if (const char* e = getenv("SOLVER_BEAM_STACKS")) beamStacks = atoi(e);
    if (const char* e = getenv("SOLVER_BEAM_W")) beamW = atoi(e);
    long long itemArea = 0;
    for (auto& it : in.items) itemArea += (long long)it.l * it.w;
    for (;;) {
        Cfg cfg;
        if (iter == 0) { cfg = Cfg{baseAnchor, baseRow, 0, 0.0, beamStacks, beamW}; }
        else {
            // bias restarts toward the simulation policies (4), which dominate;
            // keep a minority of fully random configs for diversity
            uniform_int_distribution<int> P4(0, 4), P3(0, 2);
            uniform_real_distribution<double> PR(0, 0.35), U(0, 1);
            int ap, rp;
            if (U(rng) < biasSim) { ap = 4; rp = 4; }
            else { ap = P4(rng); rp = P4(rng); }
            cfg = Cfg{ap, rp, P3(rng), PR(rng), beamStacks, beamW};
        }
        Builder b(in, seed * 1000003ULL + iter * 7919ULL, cfg);
        Solution s = b.construct();
        iter++;
        if (s.ok && (!best.ok || s.waste < best.waste)) {
            best = std::move(s);
            emitSolution(best, in.pr, output);
            double el = chrono::duration<double>(Clock::now() - t0).count();
            tLastImprove = el;
            printf("iter=%lld waste=%lld waste%%=%.4f plates=%d t=%.2f\n",
                   iter, best.waste, 100.0 * best.waste / (best.waste + itemArea), best.nPlates, el);
            fflush(stdout);
        }
        double el = chrono::duration<double>(Clock::now() - t0).count();
        if (el > budget) break;
        if (el - tLastImprove > stagSec) break;
    }
    if (!best.ok) {
        fprintf(stderr, "no feasible solution found\n");
        return 1;
    }
    return 0;
}
