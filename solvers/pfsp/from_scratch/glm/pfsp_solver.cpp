// PFSP makespan solver, designed from scratch for the GLM from-scratch experiment.
// Architecture: NEH construction + Taillard-accelerated insertion local search
// + iterated greedy (destroy / greedy reconstruction / local search / SA acceptance).
// Single-threaded, deterministic given --seed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <climits>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>

using namespace std;
using Clock = chrono::steady_clock;

static double nowSec(const Clock::time_point& t0) {
    return chrono::duration<double>(Clock::now() - t0).count();
}

struct Params {
    int    destroy    = 0;      // 0 => auto (1)
    double tempFrac   = 0.0005; // T0 = tempFrac * initial local-optimum makespan
    long   stagnLimit = 12000;  // iterations without global improvement before cur=best+kick
    int    kickDestroy= 0;      // destroy size used for the kick iteration (0 = same as destroy)
    double tempDecay  = 1.0;    // T decays geometrically to T0*tempDecay over the budget
    double cpBias     = 0.0;    // probability of picking destroy positions from the critical path
    bool   dnlb       = false;  // don't-look-bits in local search (reset per LS call)
    int    poolSize   = 0;      // memetic elite pool size (0 = off)
    double poolMinDist= 0.30;   // min Hamming distance fraction for pool admission
    bool   randomScan = true;   // random job scan order in local search
    int    swapEvery  = 1;      // apply windowed swap polish every N IG iterations (0=off)
    int    swapWin    = 20;     // swap polish window
    int    pullMode   = 0;      // destroy partner slot: 0=tail slot, 1=uniform random slot
    double segProb    = 0.0;    // probability of a segment-reversal shake instead of destroy
    double stagnSecs  = 0.0;    // if >0: reset when no global improvement for this many seconds
    double stagnFrac  = 0.0;    // if >0: stagnSecs = stagnFrac * time-limit
    int    ptWalkers  = 0;      // if 2: run a second walker at higher temperature (poor-man tempering)
    double ptTFactor  = 4.0;    // temperature multiplier of walker 2
    int    ptSwapEvery= 200;    // attempt walker exchange every N iterations
    int    insWin     = 25;      // if >0: with this prob restrict reinsertion to a window around removal
    bool   verbose    = false;
};

struct Solver {
    int n = 0, m = 0;
    vector<int> pt;            // n*m processing times, pt[j*m+k]
    vector<int> G;             // (n+2)*m tail-matrix workspace
    vector<int> Frow;          // m rolling head row
    vector<int> hscratch;      // m scratch for full evaluation
    vector<int> scan;          // n local-search scan order
    vector<int> Qmat;          // (n+1)*m head matrix for critical-path tracing
    vector<int> cpPos;         // critical-path positions workspace
    vector<char> dnlb;         // don't-look bits for local search
    vector<int> Gt;            // (n+1)*m tails for swap polish
    vector<int> hrow;          // m scratch for swap polish
    mt19937_64 rng;
    Clock::time_point t0;
    double deadline = 1e18;
    Params P;
    long iter = 0;

    void init(int n_, int m_, uint64_t seed) {
        n = n_; m = m_;
        G.assign((size_t)(n + 2) * m, 0);
        Frow.assign(m, 0);
        hscratch.assign(m, 0);
        scan.resize(n);
        iota(scan.begin(), scan.end(), 0);
        Qmat.assign((size_t)(n + 1) * m, 0);
        cpPos.reserve(n);
        Gt.assign((size_t)(n + 1) * m, 0);
        hrow.assign(m, 0);
        rng.seed(seed);
        t0 = Clock::now();
    }
    inline bool timeLeft() { return nowSec(t0) < deadline; }

    // Full makespan of a complete sequence.
    int makespan(const vector<int>& seq) {
        int* h = hscratch.data();
        for (int k = 0; k < m; k++) h[k] = 0;
        for (int i = 0; i < n; i++) {
            const int* p = &pt[(size_t)seq[i] * m];
            for (int k = 0; k < m; k++) h[k] = max(h[k], (k ? h[k - 1] : 0)) + p[k];
        }
        return h[m - 1];
    }

    // Evaluate best insertion of an external job into seq[0..len-1].
    // Returns cost after insertion; bestS = insertion position in 0..len.
    int bestInsert(const vector<int>& seq, int len, int job, int& bestS) {
        int* Gm = G.data();
        for (int k = 0; k < m; k++) Gm[(size_t)len * m + k] = 0;
        for (int i = len - 1; i >= 0; i--) {
            const int* pj = &pt[(size_t)seq[i] * m];
            int* cur = &Gm[(size_t)i * m];
            const int* nxt = &Gm[(size_t)(i + 1) * m];
            for (int k = m - 1; k >= 0; k--)
                cur[k] = pj[k] + max(nxt[k], (k + 1 < m ? cur[k + 1] : 0));
        }
        const int* pj = &pt[(size_t)job * m];
        int* F = Frow.data();
        for (int k = 0; k < m; k++) F[k] = 0;
        int best = INT_MAX; bestS = 0;
        for (int s = 0; s <= len; s++) {
            if (s > 0) {
                const int* p2 = &pt[(size_t)seq[s - 1] * m];
                for (int k = 0; k < m; k++) F[k] = max(F[k], (k ? F[k - 1] : 0)) + p2[k];
            }
            const int* Gn = &Gm[(size_t)s * m];
            int prev = 0, cost = 0;
            for (int k = 0; k < m; k++) {
                int c = max(prev, F[k]) + pj[k];
                prev = c;
                cost = max(cost, c + Gn[k]);
            }
            if (cost < best) { best = cost; bestS = s; }
        }
        return best;
    }

    // Windowed variant of bestInsert: insertion positions limited to
    // [max(0,pos-W), min(len,pos+W)] around the removal position pos.
    int bestInsertWin(const vector<int>& seq, int len, int job, int pos, int W, int& bestS) {
        int* Gm = G.data();
        for (int k = 0; k < m; k++) Gm[(size_t)len * m + k] = 0;
        for (int i = len - 1; i >= 0; i--) {
            const int* pj = &pt[(size_t)seq[i] * m];
            int* cur = &Gm[(size_t)i * m];
            const int* nxt = &Gm[(size_t)(i + 1) * m];
            for (int k = m - 1; k >= 0; k--)
                cur[k] = pj[k] + max(nxt[k], (k + 1 < m ? cur[k + 1] : 0));
        }
        const int* pj = &pt[(size_t)job * m];
        int* F = Frow.data();
        for (int k = 0; k < m; k++) F[k] = 0;
        int lo = max(0, pos - W), hi = min(len, pos + W);
        int best = INT_MAX; bestS = lo;
        for (int s = 0; s <= len; s++) {
            if (s > 0) {
                const int* p2 = &pt[(size_t)seq[s - 1] * m];
                for (int k = 0; k < m; k++) F[k] = max(F[k], (k ? F[k - 1] : 0)) + p2[k];
            }
            if (s < lo || s > hi) continue;
            const int* Gn = &Gm[(size_t)s * m];
            int prev = 0, cost = 0;
            for (int k = 0; k < m; k++) {
                int c = max(prev, F[k]) + pj[k];
                prev = c;
                cost = max(cost, c + Gn[k]);
            }
            if (cost < best) { best = cost; bestS = s; }
        }
        return best;
    }

    // Evaluate best reinsertion of the job at position r of seq (length n).
    // Prunes at `threshold`: returns the best cost found; if none beats the
    // threshold, returns threshold and bestS = -1 (caller must not move).
    int evalReinsert(const vector<int>& seq, int r, int threshold, int& bestS) {
        int len = n - 1;
        int* Gm = G.data();
        for (int k = 0; k < m; k++) Gm[(size_t)len * m + k] = 0;
        for (int i = len - 1; i >= 0; i--) {
            int jb = (i < r) ? seq[i] : seq[i + 1];
            const int* pj = &pt[(size_t)jb * m];
            int* cur = &Gm[(size_t)i * m];
            const int* nxt = &Gm[(size_t)(i + 1) * m];
            for (int k = m - 1; k >= 0; k--)
                cur[k] = pj[k] + max(nxt[k], (k + 1 < m ? cur[k + 1] : 0));
        }
        const int* pj = &pt[(size_t)seq[r] * m];
        int* F = Frow.data();
        for (int k = 0; k < m; k++) F[k] = 0;
        int best = threshold; bestS = -1;
        for (int s = 0; s <= len; s++) {
            if (s > 0) {
                int jb = (s - 1 < r) ? seq[s - 1] : seq[s];
                const int* p2 = &pt[(size_t)jb * m];
                for (int k = 0; k < m; k++) F[k] = max(F[k], (k ? F[k - 1] : 0)) + p2[k];
            }
            const int* Gn = &Gm[(size_t)s * m];
            // Vectorizable lower bound: c_k >= F[k]+pj[k], so cost >= max_k(F[k]+pj[k]+G[k]).
            int lb = 0;
            for (int k = 0; k < m; k++) lb = max(lb, F[k] + pj[k] + Gn[k]);
            if (lb >= best) continue;
            int prev = 0, cost = 0;
            for (int k = 0; k < m; k++) {
                int c = max(prev, F[k]) + pj[k];
                prev = c;
                cost = max(cost, c + Gn[k]);
            }
            if (cost < best) { best = cost; bestS = s; }
        }
        return best;
    }

    static void applyMove(vector<int>& seq, int r, int s) {
        int tmp = seq[r];
        if (s < r) { for (int i = r; i > s; i--) seq[i] = seq[i - 1]; }
        else       { for (int i = r; i < s; i++) seq[i] = seq[i + 1]; }
        seq[s] = tmp;
    }

    inline double rand01() { return (double)(rng() >> 11) * (1.0 / 9007199254740992.0); }

    // Windowed pairwise-swap polish. A general swap of distant jobs is NOT
    // covered by the insertion neighborhood. One sweep: O(n*m) tails + O(n*W*m)
    // prefix heads + O(n*W^2*m) pair chains; restarts after each improvement.
    int swapPolish(vector<int>& seq, int curCost) {
        int W = P.swapWin;
        int* Gm = Gt.data();
        int* F = Frow.data();
        int* h = hrow.data();
        bool improvedAny = true;
        while (improvedAny && timeLeft()) {
            improvedAny = false;
            for (int k = 0; k < m; k++) Gm[(size_t)n * m + k] = 0;
            for (int i = n - 1; i >= 0; i--) {
                const int* pj = &pt[(size_t)seq[i] * m];
                int* cur = &Gm[(size_t)i * m];
                const int* nxt = &Gm[(size_t)(i + 1) * m];
                for (int k = m - 1; k >= 0; k--)
                    cur[k] = pj[k] + max(nxt[k], (k + 1 < m ? cur[k + 1] : 0));
            }
            for (int k = 0; k < m; k++) F[k] = 0;
            for (int i = 0; i + 1 < n && timeLeft(); i++) {
                if (i > 0) {
                    const int* p2 = &pt[(size_t)seq[i - 1] * m];
                    for (int k = 0; k < m; k++) F[k] = max(F[k], (k ? F[k - 1] : 0)) + p2[k];
                }
                int jmax = min(n - 1, i + W);
                for (int j = i + 1; j <= jmax; j++) {
                    for (int k = 0; k < m; k++) h[k] = F[k];
                    for (int t = i; t <= j; t++) {
                        int job = (t == i) ? seq[j] : (t == j) ? seq[i] : seq[t];
                        const int* pj = &pt[(size_t)job * m];
                        int prev = 0;
                        for (int k = 0; k < m; k++) {
                            int c = max(h[k], prev) + pj[k];
                            h[k] = c; prev = c;
                        }
                    }
                    const int* Gn = &Gm[(size_t)(j + 1) * m];
                    int cost = 0;
                    for (int k = 0; k < m; k++) cost = max(cost, h[k] + Gn[k]);
                    if (cost < curCost) {
                        swap(seq[i], seq[j]);
                        return cost;  // tails/heads stale: caller re-invokes until stable
                    }
                }
            }
        }
        return curCost;
    }

    // Drive swapPolish to a windowed local optimum.
    int swapPolishFull(vector<int>& seq, int curCost) {
        while (true) {
            int before = curCost;
            curCost = swapPolish(seq, curCost);
            if (curCost >= before || !timeLeft()) return curCost;
        }
    }

    // Job positions on one critical path of seq (O(n*m)), positions strictly decreasing.
    void criticalPositions(const vector<int>& seq, vector<int>& out) {
        int* Q = Qmat.data();
        for (int k = 0; k < m; k++) Q[k] = 0;
        for (int i = 0; i < n; i++) {
            const int* p = &pt[(size_t)seq[i] * m];
            int* row = &Q[(size_t)(i + 1) * m];
            const int* prevrow = &Q[(size_t)i * m];
            int prev = 0;
            for (int k = 0; k < m; k++) {
                int v = max(prevrow[k], prev) + p[k];
                row[k] = v; prev = v;
            }
        }
        out.clear();
        int i = n - 1, k = m - 1;
        while (i >= 0) {
            int start = Q[(size_t)(i + 1) * m + k] - pt[(size_t)seq[i] * m + k];
            if (k > 0 && start == Q[(size_t)(i + 1) * m + k - 1]) { k--; continue; }
            out.push_back(i);
            i--;
        }
    }

    // Insertion local search until local optimum or deadline.
    int localSearch(vector<int>& seq, int curCost) {
        bool improved = true;
        if (P.dnlb) dnlb.assign(n, 0);  // marks persist across passes within this LS call
        while (improved) {
            improved = false;
            if (P.randomScan) {
                for (int i = (int)scan.size() - 1; i > 0; i--) {
                    int j = (int)(rng() % (uint64_t)(i + 1));
                    swap(scan[i], scan[j]);
                }
            }
            if (P.dnlb) {
                // Don't-look-bits by position: skip positions marked as
                // non-improving; a move clears marks around its shifted window.
                for (int idx = 0; idx < n; idx++) {
                    if (!timeLeft()) return curCost;
                    int r = P.randomScan ? scan[idx] : idx;
                    if (dnlb[r]) continue;
                    int s;
                    int c = evalReinsert(seq, r, curCost, s);
                    if (s >= 0 && c < curCost) {
                        applyMove(seq, r, s);
                        curCost = c;
                        improved = true;
                        int lo = max(0, min(r, s) - 1), hi = min(n - 1, max(r, s) + 1);
                        for (int i = lo; i <= hi; i++) dnlb[i] = 0;
                    } else {
                        dnlb[r] = 1;
                    }
                }
            } else {
                for (int idx = 0; idx < n; idx++) {
                    if ((idx & 7) == 0 && !timeLeft()) return curCost;
                    int r = P.randomScan ? scan[idx] : idx;
                    int s;
                    int c = evalReinsert(seq, r, curCost, s);
                    if (s >= 0 && c < curCost) {
                        applyMove(seq, r, s);
                        curCost = c;
                        improved = true;
                    }
                }
            }
        }
        return curCost;
    }

    // NEH construction: decreasing total-processing-time order + best insertion.
    int NEH(vector<int>& seq) {
        vector<int> ord(n);
        iota(ord.begin(), ord.end(), 0);
        vector<long long> sums(n, 0);
        for (int j = 0; j < n; j++) {
            long long s = 0;
            const int* p = &pt[(size_t)j * m];
            for (int k = 0; k < m; k++) s += p[k];
            sums[j] = s;
        }
        stable_sort(ord.begin(), ord.end(), [&](int a, int b) { return sums[a] > sums[b]; });
        seq.clear(); seq.reserve(n);
        int cost = 0;
        for (int j : ord) {
            if (!timeLeft() && !seq.empty()) { /* extreme safety: append rest */ 
                for (int q : ord) { if (find(seq.begin(), seq.end(), q) == seq.end()) seq.push_back(q); }
                return makespan(seq);
            }
            int s;
            cost = bestInsert(seq, (int)seq.size(), j, s);
            seq.insert(seq.begin() + s, j);
        }
        return cost;
    }
};

static bool readInstance(const char* path, Solver& S) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    int n = 0, mm = 0;
    if (fscanf(f, "%d %d", &n, &mm) != 2) { fclose(f); return false; }
    if (n <= 0 || mm <= 0) { fclose(f); return false; }
    S.n = n; S.m = mm;
    S.pt.assign((size_t)n * mm, 0);
    for (int j = 0; j < n; j++) {
        for (int k = 0; k < mm; k++) {
            int id, v;
            if (fscanf(f, "%d %d", &id, &v) != 2) { fclose(f); return false; }
            if (id < 0 || id >= mm) { fclose(f); return false; }
            S.pt[(size_t)j * mm + id] = v;
        }
    }
    fclose(f);
    return true;
}

static void writeSolution(const char* path, const vector<int>& seq, int makespan) {
    FILE* f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot open output %s\n", path); exit(2); }
    fprintf(f, "{\"permutation\": [");
    char buf[16];
    for (size_t i = 0; i < seq.size(); i++) {
        if (i) fputc(',', f);
        snprintf(buf, sizeof buf, "%d", seq[i]);
        fputs(buf, f);
    }
    fprintf(f, "], \"makespan\": %d}\n", makespan);
    fclose(f);
}

int main(int argc, char** argv) {
    Clock::time_point tStart = Clock::now();
    const char* instancePath = nullptr;
    const char* outputPath = nullptr;
    double timeLimit = 60.0;
    uint64_t seed = 1;
    Params P;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--instance") && i + 1 < argc) instancePath = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc) outputPath = argv[++i];
        else if (!strcmp(argv[i], "--time-limit") && i + 1 < argc) timeLimit = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--destroy") && i + 1 < argc) P.destroy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--temp-frac") && i + 1 < argc) P.tempFrac = atof(argv[++i]);
        else if (!strcmp(argv[i], "--stagnation") && i + 1 < argc) P.stagnLimit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--kick-destroy") && i + 1 < argc) P.kickDestroy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--temp-decay") && i + 1 < argc) P.tempDecay = atof(argv[++i]);
        else if (!strcmp(argv[i], "--cp-bias") && i + 1 < argc) P.cpBias = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dnlb")) P.dnlb = true;
        else if (!strcmp(argv[i], "--swap-every") && i + 1 < argc) P.swapEvery = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--swap-win") && i + 1 < argc) P.swapWin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pull-mode") && i + 1 < argc) P.pullMode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seg-prob") && i + 1 < argc) P.segProb = atof(argv[++i]);
        else if (!strcmp(argv[i], "--stagn-seconds") && i + 1 < argc) P.stagnSecs = atof(argv[++i]);
        else if (!strcmp(argv[i], "--stagn-frac") && i + 1 < argc) P.stagnFrac = atof(argv[++i]);
        else if (!strcmp(argv[i], "--pt-walkers") && i + 1 < argc) P.ptWalkers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pt-tfactor") && i + 1 < argc) P.ptTFactor = atof(argv[++i]);
        else if (!strcmp(argv[i], "--pt-swap-every") && i + 1 < argc) P.ptSwapEvery = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ins-win") && i + 1 < argc) P.insWin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pool-size") && i + 1 < argc) P.poolSize = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pool-mindist") && i + 1 < argc) P.poolMinDist = atof(argv[++i]);
        else if (!strcmp(argv[i], "--sequential-scan")) P.randomScan = false;
        else if (!strcmp(argv[i], "--verbose")) P.verbose = true;
    }
    if (!instancePath || !outputPath) {
        fprintf(stderr, "usage: %s --instance P --output P --time-limit S --seed N\n", argv[0]);
        return 3;
    }
    if (P.stagnFrac > 0.0) P.stagnSecs = P.stagnFrac * timeLimit;
    double margin = 0.35 + 0.006 * timeLimit;
    double searchDeadlineSec = timeLimit - margin;

    Solver S;
    if (!readInstance(instancePath, S)) { fprintf(stderr, "failed to read instance\n"); return 4; }
    S.init(S.n, S.m, seed);
    S.t0 = tStart;  // budget covers everything from process start (input read done just before)
    S.P = P;
    if (S.P.destroy <= 0) S.P.destroy = 1;
    S.deadline = nowSec(S.t0) + searchDeadlineSec;

    // Construction + local search.
    vector<int> cur, best;
    int cost = S.NEH(cur);
    cost = S.localSearch(cur, cost);
    best = cur;
    int bestCost = cost, curCost = cost;
    writeSolution(outputPath, best, bestCost);  // safety write: a valid solution exists from now on

    double T0 = S.P.tempFrac * (double)curCost;
    if (T0 <= 0) T0 = 1.0;
    long sinceBest = 0;
    double lastBestWall = 0.0;
    bool kick = false;
    vector<int> cand, removedJobs;
    vector<int> remPos;
    vector<char> used;
    vector<int> partial;
    uniform_real_distribution<double> u01(0.0, 1.0);
    int d = S.P.destroy;

    // --- Memetic elite pool (optional) ---
    struct PoolSol { vector<int> seq; int cost; };
    vector<PoolSol> pool;
    vector<int> posB(S.n), child(S.n);
    vector<char> inSlice(S.n);
    int resetCount = 0;
    auto hamming = [&](const vector<int>& a, const vector<int>& b) {
        for (int i = 0; i < S.n; i++) posB[b[i]] = i;
        int mism = 0;
        for (int i = 0; i < S.n; i++) if (a[i] != b[i]) mism++;
        return (double)mism / S.n;
    };
    auto tryPoolInsert = [&](const vector<int>& seq, int c) {
        if ((int)pool.size() < S.P.poolSize) {
            for (auto& ps : pool) if (hamming(seq, ps.seq) < S.P.poolMinDist) return;
            pool.push_back({seq, c});
            return;
        }
        int worst = 0;
        for (int i = 1; i < (int)pool.size(); i++) if (pool[i].cost > pool[worst].cost) worst = i;
        if (c >= pool[worst].cost) return;
        for (auto& ps : pool) if (hamming(seq, ps.seq) < S.P.poolMinDist) return;
        pool[worst] = {seq, c};
    };
    if (S.P.poolSize > 1) {
        tryPoolInsert(best, bestCost);
        // Seed the pool with diverse kicked local optima.
        vector<int> p2, work = best;
        for (int attempt = 0; (int)pool.size() < S.P.poolSize && attempt < 4 * S.P.poolSize && S.timeLeft(); attempt++) {
            p2 = work;
            int dd = 6 + (int)(S.rng() % 5);
            for (int i = 0; i < dd; i++) swap(p2[(int)(S.rng() % (uint64_t)S.n)], p2[(int)(S.rng() % (uint64_t)S.n)]);
            int c2 = S.makespan(p2);
            c2 = S.localSearch(p2, c2);
            tryPoolInsert(p2, c2);
        }
        bestCost = min(bestCost, min_element(pool.begin(), pool.end(), [](const PoolSol& a, const PoolSol& b){ return a.cost < b.cost; })->cost);
        for (auto& ps : pool) if (ps.cost == bestCost) best = ps.seq;
        cur = best; curCost = bestCost;
    }
    auto orderCrossover = [&](const vector<int>& A, const vector<int>& B) {
        int i = (int)(S.rng() % (uint64_t)S.n);
        int len = 1 + (int)(S.rng() % (uint64_t)(S.n / 2 + 1));
        int j = min(S.n, i + len);
        fill(inSlice.begin(), inSlice.end(), 0);
        for (int t = i; t < j; t++) { child[t] = A[t]; inSlice[A[t]] = 1; }
        int w = 0;
        for (int t = 0; t < S.n; t++) {
            while (w >= i && w < j) w++;
            if (!inSlice[B[t]]) child[w++] = B[t];
        }
    };

    while (S.timeLeft()) {
        S.iter++;
        cand = cur;
        int candCost = 0;
        if (S.P.segProb > 0.0 && S.rand01() < S.P.segProb) {
            // Segment-reversal shake: reverse a short random segment, then LS.
            int a = (int)(S.rng() % (uint64_t)S.n);
            int len = 2 + (int)(S.rng() % 7);
            int b = min(S.n - 1, a + len);
            reverse(cand.begin() + a, cand.begin() + b + 1);
            candCost = S.makespan(cand);
            candCost = S.localSearch(cand, candCost);
            if (S.P.swapEvery > 0 && (S.iter % S.P.swapEvery) == 0)
                candCost = S.swapPolishFull(cand, candCost);
        } else {
        int dIter = kick && S.P.kickDestroy > 0 ? S.P.kickDestroy : d;
        // Destroy: swap-sampler. For each removed job: a random position is swapped
        // with a partner slot (tail slot by default; uniform slot in pullMode=1),
        // then the tail is truncated. The implicit transposition (partner job is
        // pulled into the removed slot) is a load-bearing part of the perturbation.
        removedJobs.clear();
        remPos.clear();
        if (S.P.pullMode == 1) {
            for (int i = 0; i < dIter; i++) {
                int pos = (int)(S.rng() % (uint64_t)(S.n - i));
                int q = (int)(S.rng() % (uint64_t)(S.n - i));
                if (q == pos) q = (q + 1) % (S.n - i);
                removedJobs.push_back(cand[pos]);
                remPos.push_back(pos);
                cand[pos] = cand[q];
                cand[q] = cand[S.n - 1 - i];
                cand[S.n - 1 - i] = removedJobs.back();
            }
        } else {
            for (int i = 0; i < dIter; i++) {
                int pos = (int)(S.rng() % (uint64_t)(S.n - i));
                removedJobs.push_back(cand[pos]);
                remPos.push_back(pos);
                cand[pos] = cand[S.n - 1 - i];
                cand[S.n - 1 - i] = removedJobs.back();
            }
        }
        partial = cand;
        partial.resize(S.n - dIter);
        // Shuffle reconstruction order.
        for (int i = dIter - 1; i > 0; i--) {
            int j = (int)(S.rng() % (uint64_t)(i + 1));
            swap(removedJobs[i], removedJobs[j]);
        }
        candCost = 0;
        for (int i = 0; i < dIter; i++) {
            int s;
            if (S.P.insWin > 0 && S.rand01() < 0.5)
                candCost = S.bestInsertWin(partial, (int)partial.size(), removedJobs[i], remPos[i], S.P.insWin, s);
            else
                candCost = S.bestInsert(partial, (int)partial.size(), removedJobs[i], s);
            partial.insert(partial.begin() + s, removedJobs[i]);
        }
        cand = partial;
        candCost = S.localSearch(cand, candCost);
        if (S.P.swapEvery > 0 && (S.iter % S.P.swapEvery) == 0)
            candCost = S.swapPolishFull(cand, candCost);
        }
        if (candCost < bestCost) { best = cand; bestCost = candCost; sinceBest = 0; lastBestWall = nowSec(S.t0); }
        else sinceBest++;
        if (S.P.poolSize > 1) tryPoolInsert(cand, candCost);
            double delta = (double)candCost - curCost;
        double progress = min(1.0, nowSec(S.t0) / searchDeadlineSec);
        double Tnow = T0 * pow(S.P.tempDecay, progress);
        if (S.P.ptWalkers >= 2 && (S.iter & 1)) Tnow *= S.P.ptTFactor;
        bool forceAccept = kick;
        if (forceAccept || delta <= 0.0 || u01(S.rng) < exp(-delta / Tnow)) { cur = cand; curCost = candCost; }
        kick = false;
        bool stagnant = S.P.stagnSecs > 0.0 ? (nowSec(S.t0) - lastBestWall > S.P.stagnSecs)
                                               : (sinceBest > S.P.stagnLimit);
        if (stagnant) {
            sinceBest = 0;
            resetCount++;
            if (S.P.poolSize > 1 && (int)pool.size() > 1 && (resetCount & 1) == 0 && S.timeLeft()) {
                // Memetic reset: crossover best with a random pool member, then LS.
                int pi = (int)(S.rng() % (uint64_t)pool.size());
                orderCrossover(best, pool[pi].seq);
                cur = child;
                curCost = S.makespan(cur);
                curCost = S.localSearch(cur, curCost);
                if (curCost < bestCost) { best = cur; bestCost = curCost; sinceBest = 0; }
                tryPoolInsert(cur, curCost);
            } else {
                cur = best; curCost = bestCost;
            }
            kick = true;  // force-accept one perturbation away from the reset point
        }
    }

    // Internal consistency check before final output.
    int verify = S.makespan(best);
    if (verify != bestCost) bestCost = verify;  // defensive; should never trigger
    writeSolution(outputPath, best, bestCost);
    if (S.P.verbose) {
        fprintf(stdout, "{'n': %d, 'm': %d, 'iterations': %ld, 'makespan': %d, 'destroy': %d, 'tempFrac': %g, 'wall': %.3f}\n",
                S.n, S.m, S.iter, bestCost, d, S.P.tempFrac, nowSec(tStart));
    }
    return 0;
}
