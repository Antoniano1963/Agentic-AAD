// v152: v136 + elite pool size 8 -> 12 (wider recombination memory).
// (orientation diversity in the kick).
// NN construction + Lin-Kernighan style variable-depth sequential edge exchange
// local search (path formulation, physical flips, gain criterion, don't-look
// bits), ILS with double-bridge kicks. Single search (searches_started=1).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <unordered_set>
using namespace std;

static inline double now_sec() {
    return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
}

struct RNG {
    uint64_t s;
    void seed(uint64_t x) { s = x ? x : 0x9e3779b97f4a7c15ULL; }
    inline uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    inline int randint(int n) { return (int)(next() % (uint64_t)n); }
};

struct Solver {
    int n;
    vector<double> xs, ys;
    vector<int> ids;
    vector<vector<int>> cand;
    vector<vector<int>> candD;       // cached distances to candidates
    vector<int> t, pos;
    vector<char> dlb;
    RNG rng;
    double deadline;
    bool time_up = false;

    // LK recursion state (per t1 exploration)
    int off, dirn;
    long bestImp;
    vector<int> flips;          // path-coord k values of applied flips
    vector<int> bestFlips;
    vector<int> flipCities;     // cities involved per flip (w, xw pairs)
    vector<int> bestCities;
    unordered_set<uint64_t> brokenE, addedE;
    long nodes;
    long nodeBudget = 2000;
    int maxDepth = 25;

    inline int d(int i, int j) const {
        double dx = xs[i] - xs[j], dy = ys[i] - ys[j];
        return (int)floor(sqrt(dx * dx + dy * dy) + 0.5);
    }
    static inline uint64_t ekey(int a, int b) {
        if (a > b) swap(a, b);
        return ((uint64_t)a << 32) | (uint32_t)b;
    }

    void build_candidates(int k) {
        cand.assign(n, {}); candD.assign(n, {});
        if (n <= k + 1) {
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++) if (j != i) cand[i].push_back(j);
            for (int i = 0; i < n; i++)
                sort(cand[i].begin(), cand[i].end(), [&](int a, int b){ return d(i,a) < d(i,b); });
            for (int i = 0; i < n; i++) { candD[i].clear(); for (int j : cand[i]) candD[i].push_back(d(i, j)); }
            return;
        }
        double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
        for (int i = 0; i < n; i++) {
            minx = min(minx, xs[i]); maxx = max(maxx, xs[i]);
            miny = min(miny, ys[i]); maxy = max(maxy, ys[i]);
        }
        int cells = max(1, (int)ceil(sqrt((double)n)));
        double span = max(maxx - minx, maxy - miny);
        double cs = span / cells; if (cs <= 0) cs = 1;
        int W = cells + 2;
        vector<vector<int>> grid(W * W);
        auto cellx = [&](double x){ int c = (int)((x - minx) / cs); return min(max(c, 0), W - 1); };
        auto celly = [&](double y){ int c = (int)((y - miny) / cs); return min(max(c, 0), W - 1); };
        vector<int> gx(n), gy(n);
        for (int i = 0; i < n; i++) {
            gx[i] = cellx(xs[i]); gy[i] = celly(ys[i]);
            grid[gy[i] * W + gx[i]].push_back(i);
        }
        vector<int> best(k); vector<int> bestd(k);
        for (int p = 0; p < n; p++) {
            int bn = 0, maxd = 0;
            int cx = gx[p], cy = gy[p];
            for (int r = 0; r <= W; r++) {
                if (bn >= k && (double)(r - 1) * cs > maxd) break;
                for (int yy = cy - r; yy <= cy + r; yy++) {
                    if (yy < 0 || yy >= W) continue;
                    for (int xx = cx - r; xx <= cx + r; xx++) {
                        if (xx < 0 || xx >= W) continue;
                        if (r > 0 && abs(xx - cx) != r && abs(yy - cy) != r) continue;
                        for (int q : grid[yy * W + xx]) {
                            if (q == p) continue;
                            int dd = d(p, q);
                            if (bn < k) {
                                best[bn] = q; bestd[bn] = dd; bn++;
                                if (bn == k) { maxd = 0; for (int z = 0; z < k; z++) maxd = max(maxd, bestd[z]); }
                            } else if (dd < maxd) {
                                int mi = 0; for (int z = 1; z < k; z++) if (bestd[z] > bestd[mi]) mi = z;
                                best[mi] = q; bestd[mi] = dd;
                                maxd = 0; for (int z = 0; z < k; z++) maxd = max(maxd, bestd[z]);
                            }
                        }
                    }
                }
            }
            if (bn < k) {
                vector<pair<int,int>> all; all.reserve(n);
                for (int q = 0; q < n; q++) if (q != p) all.push_back({d(p, q), q});
                int take = min(k, (int)all.size());
                nth_element(all.begin(), all.begin() + take, all.end());
                all.resize(take);
                bn = take;
                for (int z = 0; z < bn; z++) { bestd[z] = all[z].first; best[z] = all[z].second; }
            }
            vector<pair<int,int>> tmp;
            for (int z = 0; z < bn; z++) tmp.push_back({bestd[z], best[z]});
            sort(tmp.begin(), tmp.end());
            cand[p].reserve(bn); candD[p].reserve(bn);
            for (auto &e : tmp) { cand[p].push_back(e.second); candD[p].push_back(e.first); }
        }
    }

    long tour_length(const vector<int> &tt) const {
        long L = 0;
        for (int i = 0; i < n; i++) L += d(tt[i], tt[(i + 1) % n]);
        return L;
    }

    void rebuild_pos() { for (int i = 0; i < n; i++) pos[t[i]] = i; }

    void nn_construct() {
        t.clear(); t.reserve(n);
        pos.assign(n, 0);
        vector<char> used(n, 0);
        int cur = rng.randint(n);
        t.push_back(cur); used[cur] = 1;
        for (int step = 1; step < n; step++) {
            int nxt = -1, bd = INT32_MAX;
            for (int q : cand[cur]) if (!used[q]) { nxt = q; break; }
            if (nxt < 0) {
                for (int q = 0; q < n; q++) if (!used[q]) { int dd = d(cur, q); if (dd < bd) { bd = dd; nxt = q; } }
            }
            t.push_back(nxt); used[nxt] = 1; cur = nxt;
        }
        rebuild_pos();
    }

    // ---- path-coordinate helpers: path pos i maps to tour index idx(i) ----
    inline int idx(int i) const {
        int v = (off + dirn * i) % n;
        if (v < 0) v += n;
        return v;
    }
    inline int path_pos(int city) const {
        int v = dirn * (pos[city] - off) % n;
        if (v < 0) v += n;
        return v;
    }
    inline int pat(int i) const { return t[idx(i)]; }

    void flip(int lo, int hi) { // reverse path positions [lo..hi]
        while (lo < hi) {
            int a = idx(lo), b = idx(hi);
            int ca = t[a], cb = t[b];
            t[a] = cb; t[b] = ca;
            pos[cb] = a; pos[ca] = b;
            lo++; hi--;
        }
    }

    // LK recursion. Current path: t1 at path pos 0, tail at n-1. G = removed - added so far.
    void lk_level(int depth, long G) {
        if (nodes++ > nodeBudget) return;
        int tail = pat(n - 1);
        long imp = G - d(tail, pat(0));
        if (imp > bestImp) { bestImp = imp; bestFlips = flips; bestCities = flipCities; bestCities.push_back(tail); }
        if (depth >= maxDepth) return;
        // gather candidate extensions from tail
        int t1 = pat(0);
        long opts[12]; int ok_[12], ow[12]; int on = 0;
        int cn = cand[tail].size();
        for (int ci = 0; ci < cn; ci++) {
            int w = cand[tail][ci];
            if (w == t1) continue;
            int k = path_pos(w);
            if (k <= 0 || k >= n - 2) continue;
            int xw = pat(k + 1);
            if (brokenE.count(ekey(tail, w))) continue;      // would re-add a broken edge
            if (addedE.count(ekey(w, xw))) continue;         // would re-break an added edge
            long g = G + d(w, xw) - candD[tail][ci];
            if (g <= 0) continue;
            if (on < 12) { opts[on] = g; ok_[on] = k; ow[on] = w; on++; }
            else { // replace worst
                int mi = 0; for (int z = 1; z < 12; z++) if (opts[z] < opts[mi]) mi = z;
                if (g > opts[mi]) { opts[mi] = g; ok_[mi] = k; ow[mi] = w; }
            }
        }
        if (on == 0) return;
        // sort desc by gain
        for (int a = 0; a < on; a++) for (int b = a + 1; b < on; b++)
            if (opts[b] > opts[a]) { swap(opts[a], opts[b]); swap(ok_[a], ok_[b]); swap(ow[a], ow[b]); }
        int B = (depth == 1) ? 5 : (depth == 2 ? 3 : 2);
        if (on > B) on = B;
        for (int z = 0; z < on; z++) {
            int k = ok_[z], w = ow[z];
            int xw = pat(k + 1);
            brokenE.insert(ekey(w, xw));
            addedE.insert(ekey(tail, w));
            flip(k + 1, n - 1);
            flips.push_back(k);
            flipCities.push_back(w); flipCities.push_back(xw);
            lk_level(depth + 1, opts[z]);
            flipCities.pop_back(); flipCities.pop_back();
            flips.pop_back();
            flip(k + 1, n - 1); // undo
            brokenE.erase(ekey(w, xw));
            addedE.erase(ekey(tail, w));
            if (nodes > nodeBudget) return;
        }
    }

    // Explore LK moves with t1 fixed at path start, both orientations.
    // Returns improvement (>0) and applies best found move.
    long lk_from(int t1) {
        long applied = 0;
        for (int dirtry = 0; dirtry < 2; dirtry++) {
            off = pos[t1];
            dirn = (dirtry == 0) ? 1 : -1;
            // path: pos0 = t1, pos n-1 = neighbor u
            bestImp = 0; flips.clear(); bestFlips.clear(); flipCities.clear(); bestCities.clear();
            brokenE.clear(); addedE.clear(); nodes = 0;
            int u = pat(n - 1);
            long G0 = d(t1, u);
            brokenE.insert(ekey(t1, u));
            bestCities.push_back(t1);
            lk_level(1, G0);
            if (bestImp > 0) {
                // replay best flip sequence
                for (int k : bestFlips) flip(k + 1, n - 1);
                applied = bestImp;
                for (int c : bestCities) {
                    dlb[c] = 0;
                    for (int w : cand[c]) dlb[w] = 0;
                }
                break; // tour changed; positions/orientation stale
            }
        }
        return applied;
    }


    inline void reverse_seg(int lo, int hi) { // reverse t[lo..hi], lo<=hi (tour coords)
        while (lo < hi) { int a = t[lo], b = t[hi]; t[lo] = b; t[hi] = a; pos[b] = lo; pos[a] = hi; lo++; hi--; }
    }

    inline bool try2opt(int x, int y) { // break (x,next(x)) and (y,next(y)); add (x,y),(nx,ny)
        int i = pos[x], j = pos[y];
        int nx = t[(i + 1) % n], ny = t[(j + 1) % n];
        if (y == x || y == nx) return false;
        int gain = d(x, nx) + d(y, ny) - d(x, y) - d(nx, ny);
        if (gain <= 0) return false;
        int a = (i + 1) % n, b = j;
        if (a <= b) reverse_seg(a, b);
        else reverse_seg(b + 1, a - 1);
        dlb[x] = dlb[nx] = dlb[y] = dlb[ny] = 0;
        return true;
    }

    bool try_relocate(int i, int L, int j) { // move segment t[i..i+L-1] to after position j
        if (j >= i - 1 && j < i + L) return false;
        if (j == (i - 1 + n) % n) return false;
        int p = t[(i - 1 + n) % n], q = t[(i + L) % n];
        int A0 = t[i], AL = t[i + L - 1];
        int u = t[j], v = t[(j + 1) % n];
        int removed = d(p, A0) + d(AL, q) + d(u, v);
        bool rev = false;
        int added = d(p, q) + d(u, A0) + d(AL, v);
        int added2 = d(p, q) + d(u, AL) + d(A0, v);
        if (added2 < added) { added = added2; rev = true; }
        if (removed - added <= 0) return false;
        vector<int> seg(t.begin() + i, t.begin() + i + L);
        if (rev) reverse(seg.begin(), seg.end());
        t.erase(t.begin() + i, t.begin() + i + L);
        int jj = (j > i) ? j - L : j;
        t.insert(t.begin() + jj + 1, seg.begin(), seg.end());
        rebuild_pos();
        for (int c : seg) dlb[c] = 0;
        dlb[p] = dlb[q] = dlb[u] = dlb[v] = 0;
        return true;
    }

    bool try3opt(int a) { // A + rev(B) + rev(C) + D double-reversal, candidate-driven
        int i = pos[a];
        int b0 = t[(i + 1) % n];
        for (int b1 : cand[a]) {
            int j = pos[b1];
            if (j <= i + 1 || j >= n - 2) continue;
            int c0 = t[j + 1];
            long g0 = d(a, b0) + d(b1, c0); // two of the removed edges
            for (int c1 : cand[b0]) {
                int k = pos[c1];
                if (k <= j || k >= n - 1) continue;
                int d0 = t[k + 1];
                long gain = g0 + d(c1, d0) - d(a, b1) - d(b0, c1) - d(c0, d0);
                long gain2 = g0 + d(c1, d0) - d(a, c0) - d(c1, b0) - d(b1, d0); // A+C+B+D segment swap
                if (gain2 > 0 && gain2 > gain) {
                    int lenC = k - j; // segment C = t[j+1..k]
                    reverse_seg(i + 1, k);
                    reverse_seg(i + 1, i + lenC);
                    reverse_seg(i + lenC + 1, k);
                    dlb[a] = dlb[b0] = dlb[b1] = dlb[c0] = dlb[c1] = dlb[d0] = 0;
                    return true;
                }
                if (gain <= 0) continue;
                reverse_seg(i + 1, j);
                reverse_seg(j + 1, k);
                dlb[a] = dlb[b0] = dlb[b1] = dlb[c0] = dlb[c1] = dlb[d0] = 0;
                return true;
            }
        }
        return false;
    }

    // fast 2-opt + Or-opt over cities with dlb clear; returns true if improved
    bool fast_ls() {
        bool any_improved = false;
        bool any = true;
        while (any && !time_up) {
            any = false;
            for (int s = 0; s < n; s++) {
                int a = t[s];
                if (dlb[a]) continue;
                bool improved = false;
                int pa = t[(s - 1 + n) % n];
                for (int b : cand[a]) {
                    if (try2opt(a, b)) { improved = true; break; }
                    if (try2opt(pa, t[(pos[b] - 1 + n) % n])) { improved = true; break; }
                }
                if (!improved) {
                    int ia = pos[a];
                    for (int L = 1; L <= 5 && !improved; L++) {
                        if (ia + L > n) break;
                        for (int b : cand[a]) {
                            if (try_relocate(ia, L, pos[b])) { improved = true; break; }
                        }
                    }
                }
                if (!improved) improved = try3opt(a);
                if (improved) { any = true; any_improved = true; }
                else dlb[a] = 1;
                if ((s & 1023) == 0 && now_sec() > deadline) { time_up = true; return any_improved; }
            }
        }
        return any_improved;
    }

    void lk_sweep() {
        for (int c = 0; c < n; c++) {
            if (dlb[c]) continue;
            if (lk_from(c) == 0) dlb[c] = 1;
            if ((c & 255) == 0 && now_sec() > deadline) { time_up = true; return; }
        }
    }

    void local_search() { // fast 2-opt/Or-opt first, then LK; alternate while productive
        while (!time_up) {
            fast_ls();
            if (time_up) return;
            bool lk_improved = false;
            while (!time_up) {
                lk_sweep();
                if (time_up) return;
                bool any = false;
                for (int c = 0; c < n; c++) if (!dlb[c]) { any = true; break; }
                if (!any) break;
                lk_improved = true;
            }
            if (!lk_improved) return;
            bool any = false;
            for (int c = 0; c < n; c++) if (!dlb[c]) { any = true; break; }
            if (!any) return;
        }
    }

    void double_bridge() {
        if (n < 8) return;
        int q = max(1, n / 4 - 1);
        int c1 = 1 + rng.randint(q);
        int c2 = c1 + 1 + rng.randint(q);
        int c3 = c2 + 1 + rng.randint(q);
        if (c3 >= n - 1) { c1 = n / 8; c2 = n / 2 - n / 8; c3 = n / 2 + n / 8; }
        if (c1 < 1 || c2 <= c1 || c3 <= c2 || c3 >= n) return;
        vector<int> nt;
        nt.reserve(n);
        for (int i = 0; i < c1; i++) nt.push_back(t[i]);
        for (int i = c3; i < n; i++) nt.push_back(t[i]);
        for (int i = c2; i < c3; i++) nt.push_back(t[i]);
        for (int i = c1; i < c2; i++) nt.push_back(t[i]);
        t.swap(nt);
        rebuild_pos();
        // re-examine cities around the 8 changed edge endpoints
        int bnd[8] = {0, c1 - 1, c1, c2 - 1, c2, c3 - 1, c3, n - 1};
        for (int p : bnd) {
            int c = t[p];
            dlb[c] = 0;
            for (int w : cand[c]) dlb[w] = 0;
        }
    }

    void double_bridge_rev() { // double-bridge with one randomly chosen segment reversed
        if (n < 8) return;
        int q = max(1, n / 4 - 1);
        int c1 = 1 + rng.randint(q);
        int c2 = c1 + 1 + rng.randint(q);
        int c3 = c2 + 1 + rng.randint(q);
        if (c3 >= n - 1) { c1 = n / 8; c2 = n / 2 - n / 8; c3 = n / 2 + n / 8; }
        if (c1 < 1 || c2 <= c1 || c3 <= c2 || c3 >= n) return;
        vector<int> segA(t.begin(), t.begin() + c1);
        vector<int> segB(t.begin() + c1, t.begin() + c2);
        vector<int> segC(t.begin() + c2, t.begin() + c3);
        vector<int> segD(t.begin() + c3, t.end());
        int r; // reverse the LONGEST segment (bias toward breaking stretched regions)
        int lA = c1, lB = c2 - c1, lC = c3 - c2, lD = n - c3;
        r = 0; int lm = lA;
        if (lB > lm) { lm = lB; r = 1; }
        if (lC > lm) { lm = lC; r = 2; }
        if (lD > lm) { r = 3; }
        if (r == 0) reverse(segA.begin(), segA.end());
        else if (r == 1) reverse(segB.begin(), segB.end());
        else if (r == 2) reverse(segC.begin(), segC.end());
        else reverse(segD.begin(), segD.end());
        vector<int> nt; nt.reserve(n);
        for (int c : segA) nt.push_back(c);
        for (int c : segD) nt.push_back(c);
        for (int c : segC) nt.push_back(c);
        for (int c : segB) nt.push_back(c);
        t.swap(nt);
        rebuild_pos();
        int j1 = c1, j2 = c1 + (n - c3), j3 = j2 + (c3 - c2); // join positions in nt
        int bnd[8] = {0, j1 - 1, j1, j2 - 1, j2, j3 - 1, j3, n - 1};
        for (int p : bnd) {
            if (p < 0 || p >= n) continue;
            int c = t[p];
            dlb[c] = 0;
            for (int w : cand[c]) dlb[w] = 0;
        }
    }

    bool check_perm() {
        vector<char> seen(n, 0);
        for (int c : t) { if (c < 0 || c >= n || seen[c]) return false; seen[c] = 1; }
        return true;
    }

    // ---------- EAX-lite: single AB-cycle crossover + greedy 2-opt relink ----------
    // Parents: A = t (current array), B = pb. Returns offspring in out,
    // touched cities (incident to any changed edge) in touched.
    // Returns false if no usable AB-cycle found (caller falls back to a kick).
    bool eax_cross(const vector<int> &pa, const vector<int> &pb, vector<int> &out, vector<int> &touched) {
        static vector<int> aA0, aA1, aB0, aB1; // neighbor tables
        aA0.assign(n, -1); aA1.assign(n, -1); aB0.assign(n, -1); aB1.assign(n, -1);
        for (int i = 0; i < n; i++) {
            aA0[pa[i]] = pa[(i - 1 + n) % n]; aA1[pa[i]] = pa[(i + 1) % n];
            aB0[pb[i]] = pb[(i - 1 + n) % n]; aB1[pb[i]] = pb[(i + 1) % n];
        }
        static vector<int> stamp; stamp.assign(n, 0);
        int st = 0;
        for (int tries = 0; tries < 25; tries++) {
            int x0 = rng.randint(n);
            st++;
            // alternating walk: A-edge, B-edge, A-edge, ... until back at x0
            vector<pair<int,int>> remA, addB;
            int cur = x0, prev = -1;
            bool ok = false;
            for (int step = 0; step < 200; step++) {
                if ((step & 1) == 0) { // A-step
                    int u1 = aA0[cur], u2 = aA1[cur];
                    int nx = (rng.randint(2) ? u1 : u2);
                    if (nx == prev) nx = (nx == u1) ? u2 : u1;
                    remA.push_back({cur, nx});
                    prev = cur; cur = nx;
                } else { // B-step
                    int u1 = aB0[cur], u2 = aB1[cur];
                    int nb = (u1 == prev) ? u2 : u1;
                    if (nb == prev) { break; } // A,B share that edge: dead end
                    addB.push_back({cur, nb});
                    prev = cur; cur = nb;
                    if (cur == x0) { ok = true; break; }
                }
                if (stamp[cur] == st) break; // revisited -> not simple, abort try
                stamp[cur] = st;
            }
            if (!ok || remA.empty() || remA.size() > 60) continue;
            // check parallel-edge conflicts: added B-edge already in A-adjacency?
            bool conflict = false;
            for (auto &e : addB) {
                int u = e.first, v = e.second;
                // v in A-adj of u and edge not removed?
                bool inA = (aA0[u] == v || aA1[u] == v);
                bool removed = false;
                for (auto &r : remA) if ((r.first == u && r.second == v) || (r.first == v && r.second == u)) removed = true;
                if (inA && !removed) { conflict = true; break; }
            }
            if (conflict) continue;
            // build working adjacency from A
            static vector<int> ad0, ad1; ad0 = aA0; ad1 = aA1;
            auto remEdge = [&](int u, int v) {
                if (ad0[u] == v) ad0[u] = -1; else if (ad1[u] == v) ad1[u] = -1;
                if (ad0[v] == u) ad0[v] = -1; else if (ad1[v] == u) ad1[v] = -1;
            };
            auto addEdge = [&](int u, int v) {
                if (ad0[u] == -1) ad0[u] = v; else if (ad1[u] == -1) ad1[u] = v;
                if (ad0[v] == -1) ad0[v] = u; else if (ad1[v] == -1) ad1[v] = u;
            };
            for (auto &e : remA) remEdge(e.first, e.second);
            for (auto &e : addB) addEdge(e.first, e.second);
            // all vertices must have degree 2 now; verify quickly
            bool bad = false;
            for (auto &e : remA) for (int u : {e.first, e.second})
                if (ad0[u] < 0 || ad1[u] < 0) bad = true;
            if (bad) continue;
            // find components (cycles)
            static vector<int> comp; comp.assign(n, -1);
            int nc = 0;
            for (int s0 = 0; s0 < n; s0++) {
                if (comp[s0] >= 0) continue;
                int u = s0, p = -1;
                while (comp[u] < 0) {
                    comp[u] = nc;
                    int w = (ad0[u] != p) ? ad0[u] : ad1[u];
                    p = u; u = w;
                }
                nc++;
            }
            touched.clear();
            for (auto &e : remA) { touched.push_back(e.first); touched.push_back(e.second); }
            for (auto &e : addB) { touched.push_back(e.first); touched.push_back(e.second); }
            // greedy relink components via cheapest 2-opt merge
            int merges = 0;
            while (nc > 1 && merges < 1000) {
                merges++;
                long bestCost = INT64_MAX; int bu = -1, bv = -1, bnu = -1, bnv = -1;
                // scan candidate edges crossing components
                for (int u = 0; u < n; u++) {
                    int cu = comp[u];
                    for (int w : cand[u]) {
                        if (comp[w] == cu) continue;
                        for (int nu : {ad0[u], ad1[u]}) {
                            for (int nw : {ad0[w], ad1[w]}) {
                                long cost = (long)d(u, w) + d(nu, nw) - d(u, nu) - d(w, nw);
                                if (cost < bestCost) { bestCost = cost; bu = u; bv = w; bnu = nu; bnv = nw; }
                            }
                        }
                    }
                }
                if (bu < 0) break; // no cross candidate edge (should not happen)
                remEdge(bu, bnu); remEdge(bv, bnv);
                addEdge(bu, bv); addEdge(bnu, bnv);
                touched.push_back(bu); touched.push_back(bv);
                touched.push_back(bnu); touched.push_back(bnv);
                // relabel: merge comp[bv] into comp[bu]
                int keep = comp[bu], gone = comp[bv];
                for (int u = 0; u < n; u++) if (comp[u] == gone) comp[u] = keep;
                nc--;
            }
            if (nc != 1) continue;
            // walk adjacency from 0 to build array
            out.clear(); out.reserve(n);
            int u = 0, p = -1;
            for (int i = 0; i < n; i++) {
                out.push_back(u);
                int w = (ad0[u] != p) ? ad0[u] : ad1[u];
                p = u; u = w;
            }
            if ((int)out.size() != n || u != 0) continue;
            return true;
        }
        return false;
    }
};

static void usage_die() { fprintf(stderr, "bad args\n"); exit(2); }

int main(int argc, char **argv) {
    string instance, output, metapath;
    double time_limit = 0; long seed = 1; int max_restarts = 5;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        auto need = [&](const char *name) -> string { if (i + 1 >= argc) usage_die(); return argv[++i]; };
        if (a == "--instance") instance = need("--instance");
        else if (a == "--output") output = need("--output");
        else if (a == "--time-limit") time_limit = atof(need("--time-limit").c_str());
        else if (a == "--seed") seed = atol(need("--seed").c_str());
        else if (a == "--max-restarts") max_restarts = atoi(need("--max-restarts").c_str());
        else if (a == "--metadata") metapath = need("--metadata");
        else usage_die();
    }
    double t0 = now_sec();
    double margin = 5.0;
    double deadline = t0 + max(1.0, time_limit - margin);

    ifstream in(instance);
    if (!in) { fprintf(stderr, "cannot open instance\n"); return 1; }
    vector<int> ids; vector<double> xs, ys;
    {
        string line; bool section = false;
        while (getline(in, line)) {
            size_t a = line.find_first_not_of(" \t\r\n");
            if (a == string::npos) continue;
            size_t b = line.find_last_not_of(" \t\r\n");
            line = line.substr(a, b - a + 1);
            if (line == "NODE_COORD_SECTION") { section = true; continue; }
            if (line == "EOF") break;
            if (!section) continue;
            int id; double x, y;
            if (sscanf(line.c_str(), "%d %lf %lf", &id, &x, &y) != 3) continue;
            ids.push_back(id); xs.push_back(x); ys.push_back(y);
        }
    }
    Solver S;
    S.n = (int)ids.size();
    S.ids = ids; S.xs = xs; S.ys = ys;
    S.rng.seed((uint64_t)seed * 2654435761u + 12345u);
    S.deadline = deadline;

    auto write_meta = [&](long best_len, long kicks, int searches) {
        ofstream m(metapath);
        m << "{\n  \"searches_started\": " << searches << ",\n"
          << "  \"algorithm\": \"2opt/Or-opt LS + double-bridge ILS, random-walk acceptance, EAX-lite recomb, final LK polish\",\n"
          << "  \"seed\": " << seed << ",\n"
          << "  \"cities\": " << S.n << ",\n"
          << "  \"kicks\": " << kicks << ",\n"
          << "  \"best_length\": " << best_len << ",\n"
          << "  \"seconds_used\": " << (now_sec() - t0) << "\n}\n";
    };
    write_meta(0, 0, 1);

    int K = 20;
    S.build_candidates(K);
    S.nn_construct();
    S.dlb.assign(S.n, 0);
    S.local_search();
    vector<int> best = S.t;
    long best_len = S.tour_length(best);
    long kicks = 0;
    int fails = 0;
    long cur_len = S.tour_length(S.t);
    bool polished = false;
    vector<vector<int>> pool; // elite pool: recent distinct best tours
    pool.push_back(best);
    long pool_lens[12] = {best_len, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    while (!S.time_up && now_sec() < deadline) {
        if (!polished && now_sec() > t0 + 0.85 * (deadline - t0)) {
            // final polish: one full LK+fast-LS convergence on the best tour
            polished = true;
            S.t = best; S.rebuild_pos();
            S.dlb.assign(S.n, 0);
            S.local_search();
            long cur = S.tour_length(S.t);
            if (cur < best_len) { best = S.t; best_len = cur; }
            if (S.time_up) break;
            cur_len = best_len; fails = 0;
            continue;
        }
        if (fails >= 4 && pool.size() >= 2) {
            // deep stall: EAX recombination instead of further escalation
            vector<int> off, touched;
            if (S.eax_cross(best, pool[S.rng.randint((int)pool.size())], off, touched)) {                S.t = off; S.rebuild_pos();
                for (int c : touched) { S.dlb[c] = 0; for (int w : S.cand[c]) S.dlb[w] = 0; }
                fails = 0;
            } else {
                for (int z = 0; z < 3; z++) S.double_bridge();
                fails = 0;
            }
        } else {
            int strength = (fails >= 8) ? 3 : (fails >= 3 ? 2 : 1);
            for (int z = 0; z < strength; z++) {
                if (S.rng.randint(3) == 0) S.double_bridge_rev(); else S.double_bridge();
            }
        }
        S.local_search();
        kicks++;
        if (S.time_up) break;
        long cur = S.tour_length(S.t);
        if (cur < best_len) {
            best = S.t; best_len = cur; fails = 0; cur_len = cur;
            bool dup = false;
            for (long pl : pool_lens) if (pl == best_len) dup = true;
            if (!dup) {
                pool.push_back(best);
                for (int z = 11; z > 0; z--) pool_lens[z] = pool_lens[z - 1];
                pool_lens[0] = best_len;
                if ((int)pool.size() > 12) pool.erase(pool.begin());
            }
        }
        else if (cur <= cur_len + (long)(best_len * 0.0002)) {
            cur_len = cur; // random-walk: accept slightly worse current
        } else {
            S.t = best; S.rebuild_pos();
            cur_len = best_len;
            fails++;
        }
    }

    {
        ofstream out(output);
        out << "NAME : result\nTYPE : TOUR\nDIMENSION : " << S.n << "\nTOUR_SECTION\n";
        for (int i = 0; i < S.n; i++) out << S.ids[best[i]] << "\n";
        out << "-1\nEOF\n";
    }
    write_meta(best_len, kicks, 1);
    return 0;
}
