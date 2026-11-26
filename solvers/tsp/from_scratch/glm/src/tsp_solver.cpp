// TSP solver designed from scratch (no external solver code used).
// v002: v001 (NN + 2-opt/Or-opt + ILS double-bridge) + sequential Lin-Kernighan
// style variable-depth chained 2-opt moves with backtracking.
// Single-threaded. TSPLIB EUC_2D in, TOUR_SECTION out.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <chrono>
#include <fstream>
#include <sstream>

using namespace std;
typedef unsigned long long u64;

// ---------------- timing ----------------
static chrono::steady_clock::time_point T0;
static double elapsed() {
    return chrono::duration<double>(chrono::steady_clock::now() - T0).count();
}
static double SEARCH_DEADLINE = 1e18;
static int VERBOSE = 0;

// ---------------- rng ----------------
struct RNG {
    u64 s;
    RNG(u64 seed) : s(seed) {}
    u64 next() {
        u64 z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    unsigned pick(unsigned m) { return (unsigned)(next() % m); }
};
static RNG rng(1);

// ---------------- instance ----------------
static int n;
static vector<double> X, Y;
static vector<int> ids;

static inline double sqdist(int i, int j) {
    double dx = X[i] - X[j], dy = Y[i] - Y[j];
    return dx * dx + dy * dy;
}
static inline int edist(int i, int j) {
    return (int)floor(sqrt(sqdist(i, j)) + 0.5);
}

// ---------------- candidate lists ----------------
static int KNN_K;
static int KNARG = 12;
static int CAND_M = 48; // alpha scan set size (nearest by distance)
static int CAND_MODE = 2; // 0=distance knn, 1=alpha, 2=union(alpha/2 + dist)
static double SUBG_UB = 0; // upper bound for subgradient step sizes (NN tour length)
static bool KNN_DISTANCE_SORTED = true;
static vector<int> knn, knn_dist;

// ---- MST (Prim, O(n^2)) + binary lifting for path-max queries ----
// Penalized version: w(i,j) = d(i,j) + pi[i] + pi[j]; node 0 excluded (1-tree special node).
static vector<double> PEN; // node penalties (empty => zero)
static inline double wt(int i, int j) {
    double dij = edist(i, j);
    if (PEN.empty()) return dij;
    return dij + PEN[i] + PEN[j];
}
static void build_mst(vector<int>& par, vector<int>& pw) {
    par.assign(n, -1); pw.assign(n, 0);
    vector<double> key(n, 1e30);
    vector<char> inm(n, 0);
    // start from node 1 (node 0 is the 1-tree special node)
    key[1] = 0.0;
    for (int it = 1; it < n; it++) { // n-1 insertions for nodes 1..n-1
        int v = -1;
        for (int j = 1; j < n; j++)
            if (!inm[j] && (v == -1 || key[j] < key[v])) v = j;
        inm[v] = 1;
        if (par[v] >= 0) pw[v] = (int)wt(v, par[v]);
        for (int j = 1; j < n; j++) {
            if (!inm[j]) {
                double d = wt(v, j);
                if (d < key[j]) { key[j] = d; par[j] = v; }
            }
        }
    }
}

// Subgradient ascent on 1-tree lower bound (Helsgaun-style, own implementation).
// UB: upper bound tour length. Returns improved penalties in PEN.
static int SUBG_ROUNDS = 10;
static void subgradient(double UB) {
    PEN.assign(n, 0.0);
    vector<int> par, pw, deg(n);
    double bestL = -1e30;
    double lambda = 1.0;
    int noimp = 0;
    vector<double> bestPen(n, 0.0);
    for (int round = 0; round < SUBG_ROUNDS; round++) {
        build_mst(par, pw);
        fill(deg.begin(), deg.end(), 0);
        double L = 0;
        for (int v = 2; v < n; v++)
            if (par[v] >= 0) { deg[v]++; deg[par[v]]++; L += wt(v, par[v]); }
        // two cheapest penalized edges at node 0
        int b1 = -1, b2 = -1; double w1 = 1e30, w2 = 1e30;
        for (int j = 1; j < n; j++) {
            double w = wt(0, j);
            if (w < w1) { w2 = w1; b2 = b1; w1 = w; b1 = j; }
            else if (w < w2) { w2 = w; b2 = j; }
        }
        deg[0] += 2; deg[b1]++; deg[b2]++;
        L += w1 + w2;
        for (int v = 0; v < n; v++) L -= 2.0 * PEN[v];
        if (L > bestL) { bestL = L; bestPen = PEN; noimp = 0; }
        else if (++noimp >= 2) { lambda *= 0.5; noimp = 0; }
        double g2 = 0;
        for (int v = 0; v < n; v++) {
            int g = deg[v] - 2; // gradient of L wrt pi (ascent direction)
            g2 += (double)g * g;
        }
        if (g2 == 0) break; // 1-tree is a tour
        double denom = g2;
        double step = lambda * (UB - L) / denom;
        if (step < 1e-9) break;
        for (int v = 0; v < n; v++) PEN[v] += step * (double)(deg[v] - 2);
        if (VERBOSE) fprintf(stderr, "[subg] round %d L=%.1f UB=%.1f lam=%.3f\n", round, L, UB, lambda);
    }
    PEN = bestPen;
}

static int LOGN;
static vector<vector<int>> up, mx; // binary lifting

static void build_lifting(const vector<int>& par, const vector<int>& pw) {
    LOGN = 1;
    while ((1 << LOGN) < n) LOGN++;
    up.assign(LOGN + 1, vector<int>(n, -1));
    mx.assign(LOGN + 1, vector<int>(n, 0));
    for (int v = 0; v < n; v++) { up[0][v] = par[v]; mx[0][v] = pw[v]; }
    for (int k = 1; k <= LOGN; k++)
        for (int v = 0; v < n; v++) {
            int m = up[k - 1][v];
            if (m < 0) { up[k][v] = -1; mx[k][v] = mx[k - 1][v]; }
            else {
                up[k][v] = up[k - 1][m];
                mx[k][v] = max(mx[k - 1][v], mx[k - 1][m]);
            }
        }
}

static inline int depth_of(const vector<int>& dep, int v) { return dep[v]; }

static int path_max(const vector<int>& dep, int a, int b) {
    int best = 0;
    if (dep[a] < dep[b]) swap(a, b);
    int d = dep[a] - dep[b];
    for (int k = 0; k <= LOGN && d; k++, d >>= 1)
        if (d & 1) { best = max(best, mx[k][a]); a = up[k][a]; }
    if (a == b) return best;
    for (int k = LOGN; k >= 0; k--) {
        if (up[k][a] != up[k][b]) {
            best = max(best, max(mx[k][a], mx[k][b]));
            a = up[k][a]; b = up[k][b];
        }
    }
    return max(best, max(mx[0][a], mx[0][b]));
}

// alpha candidates: for each node, scan its M nearest by distance, compute
// alpha(i,j) = d(i,j) - max edge on MST path (i,j), 0 for MST edges; keep k best by alpha.
// CAND_MODE: 0=distance knn only; 1=alpha only; 2=union(alpha top k/2, dist top k/2)
static void build_knn(int k) {
    k = min(k, n - 1);
    KNN_K = k;
    knn.assign((size_t)k * n, -1);
    knn_dist.assign((size_t)k * n, 0);
    int M = min(CAND_M, n - 1);
    KNN_DISTANCE_SORTED = (CAND_MODE == 0 || n < 2000);
    if (CAND_MODE == 0 || n < 2000) { // plain exact k-NN by distance
        vector<double> buf(n);
        vector<int> idx(n);
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) buf[j] = (j == i) ? -1.0 : sqdist(i, j);
            for (int j = 0; j < n; j++) idx[j] = j;
            nth_element(idx.begin(), idx.begin() + k, idx.end(),
                        [&](int a, int b) { return buf[a] < buf[b]; });
            sort(idx.begin(), idx.begin() + k, [&](int a, int b) { return buf[a] < buf[b]; });
            for (int t = 0; t < k; t++) {
                int j = idx[t];
                knn[(size_t)i * k + t] = j;
                knn_dist[(size_t)i * k + t] = edist(i, j);
            }
        }
        return;
    }
    vector<int> par, pw, dep(n, 0);
    if (SUBG_ROUNDS > 0 && n >= 2000) subgradient(SUBG_UB);
    build_mst(par, pw);
    // BFS from root 1 (node 0 excluded from tree) for depths
    {
        vector<int> order;
        order.reserve(n);
        vector<vector<int>> ch(n);
        for (int v = 1; v < n; v++)
            if (par[v] >= 0 && par[v] != v) ch[par[v]].push_back(v);
        order.push_back(1);
        for (size_t qi = 0; qi < order.size(); qi++) {
            int v = order[qi];
            for (int c : ch[v]) { dep[c] = dep[v] + 1; order.push_back(c); }
        }
    }
    build_lifting(par, pw);
    // scan sets: M nearest by distance; compute alpha for each
    vector<double> buf(n);
    vector<int> idx(n);
    vector<pair<long long, int>> cand; // (alpha, city)
    vector<int> out;             // merged candidate output
    vector<char> seen(n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) buf[j] = (j == i) ? -1.0 : sqdist(i, j);
        for (int j = 0; j < n; j++) idx[j] = j;
        nth_element(idx.begin(), idx.begin() + M, idx.end(),
                    [&](int a, int b) { return buf[a] < buf[b]; });
        cand.clear();
        for (int t = 0; t < M; t++) {
            int j = idx[t];
            long long a;
            if (i == 0 || j == 0) a = edist(i, j); // special node: distance only
            else if (par[j] == i || par[i] == j) a = 0;
            else a = (long long)floor(wt(i, j)) - path_max(dep, i, j);
            cand.push_back({a, j});
        }
        sort(cand.begin(), cand.end());
        // idx[0..M) currently unordered beyond M; sort first k by distance for mode 2
        sort(idx.begin(), idx.begin() + min(M, k),
             [&](int a, int b) { return buf[a] < buf[b]; });
        out.clear();
        fill(seen.begin(), seen.end(), 0);
        int half = k / 2;
        if (CAND_MODE == 1) {
            for (int t = 0; t < k; t++) out.push_back(cand[t].second), seen[cand[t].second] = 1;
        } else { // union: alpha top half, then distance top half, then fill from alpha
            for (int t = 0; t < half; t++) out.push_back(cand[t].second), seen[cand[t].second] = 1;
            for (int t = 0; t < M && (int)out.size() < k; t++) {
                int j = idx[t];
                if (!seen[j]) { out.push_back(j); seen[j] = 1; }
            }
            for (int t = half; t < M && (int)out.size() < k; t++) {
                int j = cand[t].second;
                if (!seen[j]) { out.push_back(j); seen[j] = 1; }
            }
        }
        for (int t = 0; t < k && t < (int)out.size(); t++) {
            knn[(size_t)i * k + t] = out[t];
            knn_dist[(size_t)i * k + t] = edist(i, out[t]);
        }
    }
}

// ---------------- tour state ----------------
static vector<int> tour, pos;
static long long cur_len = 0;
static vector<char> dlb;

static inline int cityAt(int p) { return tour[(p + n) % n]; }
static inline int nxt(int c) { return tour[(pos[c] + 1) % n]; }
static inline int prv(int c) { return tour[(pos[c] - 1 + n) % n]; }

static long long tour_length(const vector<int>& t) {
    long long L = 0;
    for (int i = 0; i < n; i++) L += edist(t[i], t[(i + 1) % n]);
    return L;
}

static void reverse_seg(int l, int r) {
    while (l < r) {
        int a = tour[l], b = tour[r];
        tour[l] = b; tour[r] = a;
        pos[a] = r; pos[b] = l;
        ++l; --r;
    }
}

// ---------------- construction ----------------
static void greedy_nn(int start) {
    vector<char> vis(n, 0);
    tour.assign(n, 0);
    int cur = start, cnt = 0;
    vis[cur] = 1; tour[0] = cur;
    while (++cnt < n) {
        int best = -1;
        double bd = 1e30;
        for (int j = 0; j < n; j++) {
            if (vis[j]) continue;
            double d = sqdist(cur, j);
            if (d < bd) { bd = d; best = j; }
        }
        vis[best] = 1;
        tour[cnt] = best;
        cur = best;
    }
    for (int p = 0; p < n; p++) pos[tour[p]] = p;
}

// ---------------- 2-opt pass (candidate + DLB) ----------------
static bool two_opt_pass() {
    bool any = false;
    for (int p = 0; p < n; p++) {
        if (elapsed() > SEARCH_DEADLINE) return any;
        int a = tour[p];
        if (dlb[a]) continue;
        bool improved_a = false;
        int b = cityAt(p + 1);
        int dab = edist(a, b);
        const int* cand = &knn[(size_t)a * KNN_K];
        const int* cd = &knn_dist[(size_t)a * KNN_K];
        for (int t = 0; t < KNN_K; t++) {
            int c = cand[t];
            if (c == b) continue;
            int q = pos[c];
            int d = cityAt(q + 1);
            if (d == a || d == b) continue;
            int gain = dab + edist(c, d) - cd[t] - edist(b, d);
            if (gain > 0) {
                cur_len -= gain;
                reverse_seg(min(p, q) + 1, max(p, q));
                any = true;
                improved_a = true;
                dlb[a] = 0; dlb[b] = 0; dlb[c] = 0; dlb[d] = 0;
                p = pos[a];
                b = cityAt(p + 1);
                dab = edist(a, b);
                t = -1;
                continue;
            }
        }
        if (!improved_a) dlb[a] = 1;
    }
    return any;
}

// ---------------- Or-opt ----------------
static bool oropt_pass() {
    bool any = false;
    for (int s = 1; s <= 3; s++) {
        for (int p = 0; p + s <= n; p++) {
            if (elapsed() > SEARCH_DEADLINE) return any;
            int st = tour[p], en = tour[p + s - 1];
            int pv = cityAt(p - 1), nx = cityAt(p + s);
            if (pv == en || nx == st) continue;
            int del1 = edist(pv, st), del2 = edist(en, nx);
            for (int side = 0; side < 2; side++) {
                int hub = side ? en : st;
                const int* cand = &knn[(size_t)hub * KNN_K];
                for (int t = 0; t < KNN_K; t++) {
                    int c = cand[t];
                    int pc = pos[c];
                    if (pc >= p - 1 && pc <= p + s) continue;
                    int cn = cityAt(pc + 1);
                    int pcn = pos[cn];
                    if (pcn >= p && pcn <= p + s - 1) continue;
                    int base = del1 + del2 - edist(pv, nx);
                    int g1 = base + edist(c, cn) - edist(c, st) - edist(en, cn);
                    int g2 = base + edist(c, cn) - edist(c, en) - edist(st, cn);
                    int use = 0, g = 0;
                    if (g1 > 0 && (g2 <= 0 || g1 >= g2)) { use = 1; g = g1; }
                    else if (g2 > 0) { use = 2; g = g2; }
                    if (use) {
                        vector<int> seg(tour.begin() + p, tour.begin() + p + s);
                        if (use == 2) reverse(seg.begin(), seg.end());
                        tour.erase(tour.begin() + p, tour.begin() + p + s);
                        int inspos = (pc > p + s - 1) ? pc - s : pc;
                        tour.insert(tour.begin() + inspos + 1, seg.begin(), seg.end());
                        for (int z = 0; z < n; z++) pos[tour[z]] = z;
                        cur_len -= g;
                        any = true;
                        goto next_seg;
                    }
                }
            }
        next_seg:;
        }
    }
    return any;
}

// ---------------- Lin-Kernighan style chained 2-opt ----------------
// Trial: root t1, first x-edge (t1,t2). Each step removes closing edge (u,t1) plus
// one more edge (c,d), adds y-edge (u,c) and new closing (d,t1). Tour stays a valid
// cycle after every applied step. Accept when cur_len drops below trial start L0.
static int MAX_LK_DEPTH = 7;
static int LK_BREADTH1 = 12, LK_BREADTH2 = 8;
static int LK_BREADTH3 = 5; // depth >= 3
static vector<char> in_trial;
static vector<int> trial_nodes;
static long long L0;
// diagnostics
static long long DBG_trials = 0, DBG_accepts = 0, DBG_steps = 0;


static bool lk_extend(int u, int depth) {
    if (elapsed() > SEARCH_DEADLINE) return false;
    const int t1 = trial_nodes[0];
    int t1_is_next = (nxt(u) == t1);
    int dut1 = edist(u, t1);
    const int* cand = &knn[(size_t)u * KNN_K];
    const int* cd = &knn_dist[(size_t)u * KNN_K];
    int breadth = (depth == 0) ? KNN_K : (depth == 1 ? LK_BREADTH1 : (depth == 2 ? LK_BREADTH2 : LK_BREADTH3));
    for (int t = 0; t < breadth && t < KNN_K; t++) {
        int c = cand[t];
        if (depth == 0 && cd[t] >= dut1) {
            if (KNN_DISTANCE_SORTED) break; // distance-sorted list: prune rest
            else continue; // y1 must be shorter than x1 (alpha-sorted list)
        }
        if (in_trial[c]) continue;
        if (c == nxt(u) || c == prv(u)) continue;
        int a, cc, d;
        if (t1_is_next) { d = nxt(c); a = u; cc = c; }
        else            { d = prv(c); a = t1; cc = d; }
        if (in_trial[d]) continue;
        int delta = dut1 + edist(c, d) - cd[t] - edist(d, t1);
        // partial gain prune: G' = (L0 - cur_len + delta) + d(d,t1) > 0
        if (cur_len - delta - edist(d, t1) >= L0) continue;
        int pa = pos[a], pc2 = pos[cc];
        if (depth >= 2 && abs(pa - pc2) > n / 12) continue; // cap deep-chain reversal cost
        int l = min(pa, pc2) + 1, r = max(pa, pc2);
        long long save_len = cur_len;
        cur_len -= delta;
        reverse_seg(l, r);
        in_trial[c] = 1; in_trial[d] = 1;
        trial_nodes.push_back(c); trial_nodes.push_back(d);
        DBG_steps++;
        if (cur_len < L0) return true; // accepted, keep applied moves
        if (depth + 1 < MAX_LK_DEPTH && lk_extend(d, depth + 1)) return true;
        reverse_seg(l, r); // undo
        cur_len = save_len;
        in_trial[c] = 0; in_trial[d] = 0;
        trial_nodes.pop_back(); trial_nodes.pop_back();
    }
    return false;
}

static bool lk_trial(int t1) {
    DBG_trials++;
    for (int dir = 0; dir < 2; dir++) {
        int t2 = dir ? prv(t1) : nxt(t1);
        int x1 = edist(t1, t2);
        if (x1 <= 0) continue;
        in_trial[t1] = 1; in_trial[t2] = 1;
        trial_nodes.clear();
        trial_nodes.push_back(t1); trial_nodes.push_back(t2);
        L0 = cur_len;
        if (lk_extend(t2, 0)) { DBG_accepts++; return true; }
        for (int v : trial_nodes) in_trial[v] = 0;
        trial_nodes.clear();
    }
    return false;
}

static bool lk_pass() {
    bool any = false;
    fill(dlb.begin(), dlb.end(), 0); // two_opt_pass leaves DLB set; LK needs fresh bits
    for (int i = 0; i < n; i++) {
        if (elapsed() > SEARCH_DEADLINE) return any;
        int t1 = tour[i];
        if (dlb[t1]) continue;
        if (lk_trial(t1)) {
            any = true;
            // clear dlb of chain nodes and their current tour neighbors
            for (int v : trial_nodes) {
                in_trial[v] = 0;
                dlb[v] = 0;
            }
            for (int v : trial_nodes) {
                dlb[nxt(v)] = 0; dlb[prv(v)] = 0;
            }
            trial_nodes.clear();
        } else {
            dlb[t1] = 1;
        }
    }
    return any;
}

static void lk_optimize() {
    while (elapsed() < SEARCH_DEADLINE) {
        bool imp2 = two_opt_pass();
        bool imp1 = lk_pass();
        if (!imp1 && !imp2) return;
    }
}

// ---------------- local search / ILS ----------------
static vector<int> best_tour;
static long long best_len;
static int KICK_DIV = 4;      // double-bridge segment scale: L = n / KICK_DIV
static int ACCEPT_MODE = 0;   // 0 strict better, 1 better-or-equal, 2 threshold walk
static double ACCEPT_THRESH = 0.0008; // threshold fraction for mode 2
static int STAG_BIG = 60;     // after this many non-improving iters, add extra bridge
static int STAG_HUGE = 240;   // after this many, add two extra bridges

static void double_bridge() {
    int L = max(2, n / KICK_DIV);
    if (n < 8) return;
    int i = 1 + (int)rng.pick((unsigned)L);
    int j = i + 1 + (int)rng.pick((unsigned)L);
    int k = j + 1 + (int)rng.pick((unsigned)L);
    if (k >= n) return;
    vector<int> nt;
    nt.reserve(n);
    nt.insert(nt.end(), tour.begin(), tour.begin() + i);
    nt.insert(nt.end(), tour.begin() + j, tour.begin() + k);
    nt.insert(nt.end(), tour.begin() + i, tour.begin() + j);
    nt.insert(nt.end(), tour.begin() + k, tour.end());
    tour.swap(nt);
    for (int z = 0; z < n; z++) pos[tour[z]] = z;
}

static void local_search() {
    while (true) {
        if (elapsed() > SEARCH_DEADLINE) return;
        bool imp = two_opt_pass();
        if (elapsed() > SEARCH_DEADLINE) return;
        bool imp2 = oropt_pass();
        if (!imp && !imp2) return;
        fill(dlb.begin(), dlb.end(), 0);
    }
}

// ---------------- ILS with stagnation-triggered global restarts ----------------
static vector<int> gbest_tour;   // global incumbent across searches
static long long gbest_len;
static int RESTARTS_USED = 0;
static int MAX_RESTARTS_ARG = 5;
static double RESTART_IDLE_FRAC = 0.0; // 0 = restarts off (eval004 showed net negative)
static double SIG_IMPROVE_FRAC = 2e-4;  // improvement fraction considered significant
static double TL_GLOBAL = 900;

// one ILS segment; returns early if stagnating badly and restart is worthwhile
static void ils_segment() {
    best_tour = tour;
    best_len = cur_len;
    if (n < 8) return;
    long long iters = 0;
    long long since_improve = 0;
    double last_improve_t = elapsed();
    double idle_limit = RESTART_IDLE_FRAC * TL_GLOBAL;
    while (elapsed() < SEARCH_DEADLINE) {
        double_bridge();
        if (STAG_BIG > 0) {
            if (since_improve >= STAG_HUGE) double_bridge();
            else if (since_improve >= STAG_BIG) {
                double_bridge();
                double_bridge();
            }
        }
        fill(dlb.begin(), dlb.end(), 0);
        cur_len = tour_length(tour);
        local_search();
        lk_optimize();
        iters++;
        if (VERBOSE && (iters % 200 == 0))
            fprintf(stderr, "[t=%.1f] iters=%lld best=%lld cur=%lld trials=%lld accepts=%lld steps=%lld\n",
                    elapsed(), iters, best_len, cur_len, DBG_trials, DBG_accepts, DBG_steps);
        bool keep = false;
        if (cur_len < best_len) {
            // significant improvement resets the restart idle timer; tiny ones don't
            if (cur_len < best_len - (long long)(best_len * SIG_IMPROVE_FRAC)) {
                last_improve_t = elapsed();
            } else {
                last_improve_t = max(last_improve_t, elapsed() - 0.5 * idle_limit);
            }
            best_len = cur_len;
            best_tour = tour;
            since_improve = 0;
        } else {
            since_improve++;
            if (ACCEPT_MODE == 1) keep = (cur_len == best_len);
            else if (ACCEPT_MODE == 2)
                keep = (cur_len < (long long)(best_len * (1.0 + ACCEPT_THRESH)));
        }
        if (!keep) {
            tour = best_tour;
            for (int z = 0; z < n; z++) pos[tour[z]] = z;
        }
        // stagnation-triggered restart: fresh construction, incumbent preserved
        if (RESTART_IDLE_FRAC > 0 && RESTARTS_USED < MAX_RESTARTS_ARG &&
            elapsed() - last_improve_t > idle_limit &&
            elapsed() > 0.20 * TL_GLOBAL &&
            SEARCH_DEADLINE - elapsed() > 0.30 * TL_GLOBAL) {
            if (best_len < gbest_len) { gbest_len = best_len; gbest_tour = best_tour; }
            RESTARTS_USED++;
            if (VERBOSE) fprintf(stderr, "[t=%.1f] RESTART #%d (segment best=%lld, global=%lld)\n",
                                 elapsed(), RESTARTS_USED, best_len, gbest_len);
            int start = (int)rng.pick((unsigned)n);
            greedy_nn(start);
            cur_len = tour_length(tour);
            local_search();
            lk_optimize();
            best_tour = tour;
            best_len = cur_len;
            since_improve = 0;
            last_improve_t = elapsed();
            iters = 0;
        }
    }
    cur_len = best_len;
    tour = best_tour;
    for (int z = 0; z < n; z++) pos[tour[z]] = z;
}

static void ils() {
    gbest_tour = tour;
    gbest_len = cur_len;
    ils_segment();
    if (best_len < gbest_len) { gbest_len = best_len; gbest_tour = best_tour; }
    cur_len = gbest_len;
    tour = gbest_tour;
    for (int z = 0; z < n; z++) pos[tour[z]] = z;
}

// ---------------- I/O ----------------
static void read_instance(const char* path) {
    ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open instance\n"); exit(2); }
    string line;
    bool section = false;
    vector<int> rid; vector<double> rx, ry;
    while (getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        if (!section) {
            if (line.find("NODE_COORD_SECTION") != string::npos) section = true;
            continue;
        }
        if (line.find("EOF") != string::npos) break;
        istringstream ss(line);
        int id; double x, y;
        if (ss >> id >> x >> y) { rid.push_back(id); rx.push_back(x); ry.push_back(y); }
    }
    n = (int)rid.size();
    if (n < 3) { fprintf(stderr, "too small\n"); exit(2); }
    ids = rid;
    X = rx; Y = ry;
}

static void write_tour(const char* path) {
    ofstream f(path);
    f << "NAME : result\nTYPE : TOUR\nDIMENSION : " << n << "\n";
    f << "TOUR_SECTION\n";
    for (int p = 0; p < n; p++) f << ids[tour[p]] << "\n";
    f << "-1\nEOF\n";
}

int main(int argc, char** argv) {
    T0 = chrono::steady_clock::now();
    const char* inst = NULL, *outp = NULL, *meta = NULL;
    double tl = 900; long long seed = 1; int max_restarts = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--instance") && i + 1 < argc) inst = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc) outp = argv[++i];
        else if (!strcmp(argv[i], "--time-limit") && i + 1 < argc) tl = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--max-restarts") && i + 1 < argc) max_restarts = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--metadata") && i + 1 < argc) meta = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) VERBOSE = 1;
        else if (!strcmp(argv[i], "--knn") && i + 1 < argc) KNARG = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc) MAX_LK_DEPTH = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--breadth1") && i + 1 < argc) LK_BREADTH1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--breadth2") && i + 1 < argc) LK_BREADTH2 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cand-m") && i + 1 < argc) CAND_M = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cand-mode") && i + 1 < argc) CAND_MODE = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--kick") && i + 1 < argc) KICK_DIV = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--accept") && i + 1 < argc) ACCEPT_MODE = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--thresh") && i + 1 < argc) ACCEPT_THRESH = atof(argv[++i]);
        else if (!strcmp(argv[i], "--stag") && i + 1 < argc) { STAG_BIG = atoi(argv[++i]); STAG_HUGE = STAG_BIG * 4; }
        else if (!strcmp(argv[i], "--restart-idle") && i + 1 < argc) RESTART_IDLE_FRAC = atof(argv[++i]);
        else if (!strcmp(argv[i], "--breadth3") && i + 1 < argc) LK_BREADTH3 = atoi(argv[++i]);
    }
    if (!inst || !outp) { fprintf(stderr, "missing args\n"); return 2; }
    MAX_RESTARTS_ARG = max_restarts;
    TL_GLOBAL = tl;
    rng = RNG((u64)seed * 0x9E3779B97F4A7C15ULL + 12345);
    read_instance(inst);
    pos.assign(n, 0);
    dlb.assign(n, 0);
    in_trial.assign(n, 0);

    double parse_time = elapsed();
    double margin = tl > 100 ? 10.0 : max(1.0, tl * 0.05);
    SEARCH_DEADLINE = tl - margin;

    int start = (int)rng.pick((unsigned)n);
    greedy_nn(start);
    cur_len = tour_length(tour);
    SUBG_UB = (double)cur_len;
    if (VERBOSE) fprintf(stderr, "[t=%.1f] NN len=%lld\n", elapsed(), cur_len);

    build_knn(KNARG);
    if (VERBOSE) fprintf(stderr, "[t=%.1f] candidates built (subgradient done)\n", elapsed());
    local_search();
    if (VERBOSE) fprintf(stderr, "[t=%.1f] after 2opt+oropt len=%lld\n", elapsed(), cur_len);
    lk_optimize();
    if (VERBOSE) fprintf(stderr, "[t=%.1f] after LK len=%lld\n", elapsed(), cur_len);

    ils();

    write_tour(outp);
    if (meta) {
        ofstream m(meta);
        m << "{\n";
        m << "  \"searches_started\": " << (RESTARTS_USED + 1) << ",\n";
        m << "  \"final_length\": " << gbest_len << ",\n";
        m << "  \"instance_cities\": " << n << ",\n";
        m << "  \"seed\": " << seed << ",\n";
        m << "  \"time_limit\": " << tl << ",\n";
        m << "  \"parse_seconds\": " << parse_time << ",\n";
        m << "  \"restarts_used\": " << RESTARTS_USED << ",\n";
        m << "  \"total_seconds\": " << elapsed() << "\n";
        m << "}\n";
    }
    return 0;
}
