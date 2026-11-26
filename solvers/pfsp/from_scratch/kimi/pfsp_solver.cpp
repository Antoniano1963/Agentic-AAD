// PFSP solver v001 - from scratch implementation.
// Algorithm: NEH construction (Taillard acceleration) + Iterated Greedy
// (random destruction, greedy reinsertion, insertion local search,
//  simulated-annealing style acceptance). Single thread, deterministic seed.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static inline double now_sec() {
    return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    uint64_t next() { // splitmix64
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    int randint(int n) { return (int)(next() % (uint64_t)n); }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

struct Solver {
    int n = 0, m = 0;
    vector<vector<int>> p;   // p[job][machine]
    vector<int> sumP;        // total processing time per job
    long long totalP = 0;
    // scratch matrices for Taillard acceleration
    vector<vector<int>> e, q;
    vector<int> cx;

    void readInstance(const string& path) {
        ifstream in(path);
        if (!in) { fprintf(stderr, "cannot open instance\n"); exit(2); }
        in >> n >> m;
        p.assign(n, vector<int>(m, 0));
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < 2 * m; k += 2) {
                int id, t; in >> id >> t;
                p[j][id] = t;
            }
        }
        sumP.assign(n, 0);
        for (int j = 0; j < n; ++j) {
            long long s = 0;
            for (int k = 0; k < m; ++k) s += p[j][k];
            sumP[j] = (int)s; totalP += s;
        }
        e.assign(n, vector<int>(m, 0));
        q.assign(n, vector<int>(m, 0));
        cx.assign(m, 0);
    }

    // head/tail tables for sequence seq (length L)
    void computeHeadTail(const vector<int>& seq, int L) {
        for (int i = 0; i < L; ++i) {
            const vector<int>& pj = p[seq[i]];
            int prev = 0;
            for (int k = 0; k < m; ++k) {
                int up = (i > 0) ? e[i - 1][k] : 0;
                prev = (up > prev ? up : prev) + pj[k];
                e[i][k] = prev;
            }
        }
        for (int i = L - 1; i >= 0; --i) {
            const vector<int>& pj = p[seq[i]];
            int prev = 0;
            for (int k = m - 1; k >= 0; --k) {
                int down = (i + 1 < L) ? q[i + 1][k] : 0;
                prev = (down > prev ? down : prev) + pj[k];
                q[i][k] = prev;
            }
        }
    }

    int makespan(const vector<int>& seq) {
        int L = (int)seq.size();
        if (L == 0) return 0;
        computeHeadTail(seq, L);
        return e[L - 1][m - 1];
    }

    // cost of inserting job x into seq (length L, not containing x) at every
    // position 0..L; writes costs into out (size L+1). Requires e,q of seq.
    void insertCosts(const vector<int>& seq, int L, int x, vector<int>& out) {
        const vector<int>& px = p[x];
        out.resize(L + 1);
        for (int pos = 0; pos <= L; ++pos) {
            int prev = 0;
            int best = 0;
            for (int k = 0; k < m; ++k) {
                int up = (pos > 0) ? e[pos - 1][k] : 0;
                prev = (up > prev ? up : prev) + px[k];
                int tail = (pos < L) ? q[pos][k] : 0;
                int v = prev + tail;
                if (v > best) best = v;
            }
            out[pos] = best;
        }
    }

    // insert x at the position minimizing makespan; ties -> random among best
    void insertBest(vector<int>& seq, int x, Rng& rng) {
        int L = (int)seq.size();
        computeHeadTail(seq, L);
        static thread_local vector<int> costs;
        insertCosts(seq, L, x, costs);
        // v037: GRASP-like pick - with prob 0.3 choose uniformly among the
        // 3 cheapest positions, else random among best
        int pick = 0;
        if (rng.uniform() < 0.3 && L >= 2) {
            int idx[3] = {-1, -1, -1};
            for (int i = 0; i <= L; ++i) {
                for (int t = 0; t < 3; ++t) {
                    if (idx[t] < 0 || costs[i] < costs[idx[t]]) {
                        for (int u = 2; u > t; --u) idx[u] = idx[u - 1];
                        idx[t] = i;
                        break;
                    }
                }
            }
            pick = idx[rng.randint(3)];
        } else {
            int bestV = costs[0];
            for (int i = 1; i <= L; ++i) bestV = min(bestV, costs[i]);
            int cnt = 0;
            for (int i = 0; i <= L; ++i)
                if (costs[i] == bestV) { if (rng.randint(++cnt) == 0) pick = i; }
        }
        seq.insert(seq.begin() + pick, x);
    }

    vector<int> neh(Rng& rng) {
        vector<int> order(n);
        iota(order.begin(), order.end(), 0);
        sort(order.begin(), order.end(), [&](int a, int b) {
            if (sumP[a] != sumP[b]) return sumP[a] > sumP[b];
            return a < b;
        });
        vector<int> seq;
        seq.reserve(n);
        seq.push_back(order[0]);
        for (int i = 1; i < n; ++i) insertBest(seq, order[i], rng);
        return seq;
    }

    // v044: NEH with order weights sumP * (1 + noise*U(-1,1))
    vector<int> nehRandomized(Rng& rng, double noise) {
        vector<int> order(n);
        iota(order.begin(), order.end(), 0);
        vector<double> w(n);
        for (int j = 0; j < n; ++j)
            w[j] = sumP[j] * (1.0 + noise * (2.0 * rng.uniform() - 1.0));
        sort(order.begin(), order.end(), [&](int a, int b) { return w[a] > w[b]; });
        vector<int> seq;
        seq.reserve(n);
        seq.push_back(order[0]);
        for (int i = 1; i < n; ++i) insertBest(seq, order[i], rng);
        return seq;
    }

    // insertion local search, first improvement, random scan order (v022)
    bool localSearch(vector<int>& seq, double deadline, Rng& rng) {
        int cur = makespan(seq);
        bool improved = true;
        int iter = 0;
        vector<int> scan(seq.size());
        iota(scan.begin(), scan.end(), 0);
        while (improved) {
            if ((++iter & 3) == 0 && now_sec() > deadline) return false;
            improved = false;
            for (int t = (int)scan.size() - 1; t > 0; --t) {
                int j = rng.randint(t + 1);
                swap(scan[t], scan[j]);
            }
            for (int si = 0; si < (int)scan.size(); ++si) {
                int i = scan[si];
                if (i >= (int)seq.size()) continue;
                int x = seq[i];
                vector<int> rest(seq.begin(), seq.begin() + i);
                rest.insert(rest.end(), seq.begin() + i + 1, seq.end());
                int L = (int)rest.size();
                computeHeadTail(rest, L);
                static thread_local vector<int> costs;
                insertCosts(rest, L, x, costs);
                int bestPos = -1, bestV = cur - 1; // need strict improvement
                for (int pos = 0; pos <= L; ++pos) {
                    if (pos == i) continue; // same solution
                    if (costs[pos] <= bestV) { bestV = costs[pos]; bestPos = pos; }
                }
                if (bestPos >= 0) {
                    seq = rest;
                    seq.insert(seq.begin() + bestPos, x);
                    cur = bestV;
                    improved = true;
                    // v014: continue scan from next position (no restart)
                }
            }
        }
        return true;
    }

    vector<int> iteratedGreedy(vector<int> start, double deadline, Rng& rng,
                               int d, double tempParam) {
        vector<int> cur = start;
        int curMs = makespan(cur);
        vector<int> best = cur;
        int bestMs = curMs;
        double T = tempParam * (double)totalP / (10.0 * n * m);
        if (T <= 0) T = 1e-9;
        while (now_sec() < deadline) {
            // destruction: remove d distinct random jobs, keep removal order
            vector<int> partial = cur;
            vector<int> removed;
            removed.reserve(d);
            for (int t = 0; t < d && !partial.empty(); ++t) {
                int idx = rng.randint((int)partial.size());
                removed.push_back(partial[idx]);
                partial.erase(partial.begin() + idx);
            }
            // construction: greedy reinsertion in removal order
            for (int x : removed) insertBest(partial, x, rng);
            // local search
            if (!localSearch(partial, deadline, rng)) break;
            int ms = makespan(partial);
            bool accept = ms <= curMs; // v027: accept sideways moves
            if (!accept && T > 0 && (double)(ms - curMs) <= 3.0 * T) { // v047: truncated SA
                double prob = exp(-(double)(ms - curMs) / T);
                accept = rng.uniform() < prob;
            }
            if (accept) { cur = partial; curMs = ms; }
            if (ms < bestMs) { best = partial; bestMs = ms; }
        }
        return best;
    }
};

int main(int argc, char** argv) {
    string instPath, outPath;
    double timeLimit = 60.0;
    uint64_t seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        string a = argv[i];
        if (a == "--instance") instPath = argv[i + 1];
        else if (a == "--output") outPath = argv[i + 1];
        else if (a == "--time-limit") timeLimit = atof(argv[i + 1]);
        else if (a == "--seed") seed = strtoull(argv[i + 1], nullptr, 10);
    }
    if (instPath.empty() || outPath.empty()) {
        fprintf(stderr, "missing --instance/--output\n");
        return 2;
    }
    double t0 = now_sec();
    double deadline = t0 + timeLimit - 1.0; // reserve output time
    if (deadline <= t0) deadline = t0 + 0.05;

    Rng rng(seed);
    Solver S;
    S.readInstance(instPath);

    vector<int> best = S.neh(rng);
    S.localSearch(best, deadline, rng);
    // v044: second NEH start with randomly perturbed job order; keep better
    {
        vector<int> alt = S.nehRandomized(rng, 0.2);
        S.localSearch(alt, deadline, rng);
        if (S.makespan(alt) < S.makespan(best)) best = alt;
    }
    best = S.iteratedGreedy(best, deadline, rng, 4, 0.8);

    int ms = S.makespan(best);
    ofstream out(outPath);
    out << "{\"permutation\": [";
    for (int i = 0; i < (int)best.size(); ++i) {
        if (i) out << ", ";
        out << best[i];
    }
    out << "], \"makespan\": " << ms << "}\n";
    out.close();
    return 0;
}
