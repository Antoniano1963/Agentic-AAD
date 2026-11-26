// ROADEF2020 RTE outage maintenance planning - from-scratch solver v001
// Construction: randomized greedy on penalized approximate objective.
// Improvement: simulated annealing on start times with incremental evaluation.
// No external solver libraries. Single translation unit, C++17.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <sys/resource.h>

using Clock = std::chrono::steady_clock;
static Clock::time_point g_t0;
static double now_s() { return std::chrono::duration<double>(Clock::now() - g_t0).count(); }

// ---------------- cross-thread migration (global best feasible) ----------------
static std::mutex gBestMtx;
static std::vector<int> gBestStart;
static std::atomic<double> gBestObj{1e300};
static std::atomic<long> gBestGen{0};

// ---------------- fast JSON cursor ----------------
struct Cur {
    const char* p;
    const char* end;
    inline void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    inline char peek() { ws(); return p < end ? *p : 0; }
    inline void expect(char c) { ws(); if (p < end && *p == c) ++p; else { fprintf(stderr, "JSON parse error: expected '%c' near %.40s\n", c, p); exit(3); } }
    inline std::string str() {
        ws();
        if (p >= end || *p != '"') { fprintf(stderr, "JSON parse error: expected string near %.40s\n", p); exit(3); }
        ++p;
        const char* s = p;
        while (p < end && *p != '"') { if (*p == '\\') ++p; ++p; }
        std::string r(s, p - s);
        if (p < end) ++p;
        return r;
    }
    inline int ikey() { // string key that is an integer
        ws();
        if (p < end && *p == '"') ++p;
        bool neg = false; if (p < end && *p == '-') { neg = true; ++p; }
        int v = 0; while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        if (p < end && *p == '"') ++p;
        return neg ? -v : v;
    }
    inline double num() {
        ws();
        const char* s = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) ++p;
        return strtod(s, nullptr);
    }
    inline long lint() { ws(); if (p < end && *p == '"') ++p; bool neg = false; if (p < end && *p == '-') { neg = true; ++p; } long v = 0; while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; } if (p < end && *p == '"') ++p; return neg ? -v : v; }
    void skipValue() {
        ws();
        if (p >= end) return;
        char c = *p;
        if (c == '{') { ++p; while (true) { ws(); if (p < end && *p == '}') { ++p; return; } str(); ws(); if (*p == ':') ++p; skipValue(); ws(); if (p < end && *p == ',') ++p; } }
        else if (c == '[') { ++p; while (true) { ws(); if (p < end && *p == ']') { ++p; return; } skipValue(); ws(); if (p < end && *p == ',') ++p; } }
        else if (c == '"') { str(); }
        else { num(); }
    }
};

// ---------------- instance data ----------------
static int T = 0;
static int nWords = 0;
static std::vector<std::string> resNames;
static std::unordered_map<std::string, int> resIdx;
static std::vector<std::vector<double>> umax, umin; // [c][t]
static std::vector<int> scen;                       // S_t
static std::vector<int64_t> scenCum;                // prefix sums, size T+1
static double alphaP = 0.5, tauQ = 0.95;
static int N = 0;
static std::vector<std::string> inames;
static std::unordered_map<std::string, int> iidx;
static std::vector<int> tmaxV;                      // 1-based last valid start
static std::vector<std::vector<int>> deltaV;        // [i][t 0-based] size T
static std::vector<std::vector<int>> deltaRaw;      // as parsed (T unknown during parse)
static std::vector<std::vector<int64_t>> wlOff;     // [i][st0] -> offset into wl arrays
static std::vector<std::vector<int>> wlCnt;         // [i][st0] -> count
static std::vector<uint8_t> wlC;
static std::vector<uint16_t> wlT;
static std::vector<double> wlV;
static std::vector<std::vector<int64_t>> riskBlk;   // [i][st0] -> block start in riskAll
static std::vector<float> riskAll;                  // block: t ascending in window, S_t floats per t
static std::vector<std::vector<double>> m1V;        // [i][st0] sum over window of per-t mean risk
static std::vector<std::vector<double>> qexV;       // [i][st0] sum over window of standalone quantile excess
static std::vector<std::string> seasonNames;        // season id -> name
static std::unordered_map<std::string, int> seasonIdx;
static std::vector<std::vector<int>> seasonTimes;   // raw season periods (1-based)
static std::vector<std::vector<uint64_t>> seasonMask; // built after T known
struct PendExcl { std::string i1, i2, season; };
static std::vector<PendExcl> pendExcl;
static std::vector<std::vector<std::pair<int, int>>> excl; // [i] -> (j, sid)

static inline int qidx(int t) { return (int)std::ceil(scen[t] * tauQ) - 1; }

// ---------------- parsing ----------------
static void parseInstance(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf;
    buf.resize(sz);
    size_t rd = fread(&buf[0], 1, sz, f);
    fclose(f);
    if ((long)rd != sz) { fprintf(stderr, "short read of %s\n", path); exit(2); }
    Cur c{buf.data(), buf.data() + sz};

    c.expect('{');
    while (true) {
        c.ws();
        if (c.peek() == '}') { c.expect('}'); break; }
        std::string key = c.str();
        c.expect(':');
        if (key == "T") { T = (int)c.num(); }
        else if (key == "Alpha") { alphaP = c.num(); }
        else if (key == "Quantile") { tauQ = c.num(); }
        else if (key == "Scenarios_number") {
            c.expect('[');
            while (true) { c.ws(); if (c.peek() == ']') { c.expect(']'); break; } scen.push_back((int)c.lint()); c.ws(); if (c.peek() == ',') c.expect(','); }
        }
        else if (key == "Resources") {
            c.expect('{');
            while (true) {
                c.ws();
                if (c.peek() == '}') { c.expect('}'); break; }
                std::string rn = c.str();
                c.expect(':');
                int id = (int)resNames.size();
                resIdx[rn] = id;
                resNames.push_back(rn);
                umax.emplace_back();
                umin.emplace_back();
                c.expect('{');
                while (true) {
                    c.ws();
                    if (c.peek() == '}') { c.expect('}'); break; }
                    std::string k2 = c.str();
                    c.expect(':');
                    if (k2 == "max" || k2 == "min") {
                        std::vector<double>& dst = (k2 == "max") ? umax[id] : umin[id];
                        c.expect('[');
                        while (true) { c.ws(); if (c.peek() == ']') { c.expect(']'); break; } dst.push_back(c.num()); c.ws(); if (c.peek() == ',') c.expect(','); }
                    } else c.skipValue();
                    c.ws();
                    if (c.peek() == ',') c.expect(',');
                }
                c.ws();
                if (c.peek() == ',') c.expect(',');
            }
        }
        else if (key == "Seasons") {
            c.expect('{');
            while (true) {
                c.ws();
                if (c.peek() == '}') { c.expect('}'); break; }
                std::string sn = c.str();
                c.expect(':');
                int sid = (int)seasonNames.size();
                seasonIdx[sn] = sid;
                seasonNames.push_back(sn);
                seasonTimes.emplace_back();
                c.expect('[');
                while (true) { c.ws(); if (c.peek() == ']') { c.expect(']'); break; } seasonTimes.back().push_back(c.ikey()); c.ws(); if (c.peek() == ',') c.expect(','); }
                c.ws();
                if (c.peek() == ',') c.expect(',');
            }
        }
        else if (key == "Interventions") {
            c.expect('{');
            while (true) {
                c.ws();
                if (c.peek() == '}') { c.expect('}'); break; }
                std::string in = c.str();
                c.expect(':');
                int id = (int)inames.size();
                iidx[in] = id;
                inames.push_back(in);
                tmaxV.push_back(0);
                struct WEnt { int c, st, t; double v; };
                std::vector<WEnt> went;
                struct REnt { int st, t; int64_t off; int len; };
                std::vector<REnt> rent;
                std::vector<float> rvals;
                int itmax = 0;
                c.expect('{');
                while (true) {
                    c.ws();
                    if (c.peek() == '}') { c.expect('}'); break; }
                    std::string k2 = c.str();
                    c.expect(':');
                    if (k2 == "tmax") {
                        c.ws();
                        if (c.peek() == '"') { c.ws(); ++c.p; itmax = 0; while (c.p < c.end && *c.p >= '0' && *c.p <= '9') { itmax = itmax * 10 + (*c.p - '0'); ++c.p; } if (c.p < c.end && *c.p == '"') ++c.p; }
                        else itmax = (int)c.lint();
                        tmaxV[id] = itmax;
                    }
                    else if (k2 == "Delta") {
                        c.expect('[');
                        deltaRaw.emplace_back();
                        while (true) { c.ws(); if (c.peek() == ']') { c.expect(']'); break; } deltaRaw.back().push_back((int)c.lint()); c.ws(); if (c.peek() == ',') c.expect(','); }
                    }
                    else if (k2 == "workload") {
                        c.expect('{');
                        while (true) {
                            c.ws();
                            if (c.peek() == '}') { c.expect('}'); break; }
                            std::string rn = c.str();
                            c.expect(':');
                            auto it = resIdx.find(rn);
                            if (it == resIdx.end()) { fprintf(stderr, "unknown resource %s\n", rn.c_str()); exit(3); }
                            int cid = it->second;
                            c.expect('{');
                            while (true) { // t -> {st -> v}
                                c.ws();
                                if (c.peek() == '}') { c.expect('}'); break; }
                                int tt = c.ikey();
                                c.expect(':');
                                c.expect('{');
                                while (true) {
                                    c.ws();
                                    if (c.peek() == '}') { c.expect('}'); break; }
                                    int st = c.ikey();
                                    c.expect(':');
                                    double v = c.num();
                                    went.push_back({cid, st - 1, tt - 1, v});
                                    c.ws();
                                    if (c.peek() == ',') c.expect(',');
                                }
                                c.ws();
                                if (c.peek() == ',') c.expect(',');
                            }
                            c.ws();
                            if (c.peek() == ',') c.expect(',');
                        }
                    }
                    else if (k2 == "risk") {
                        c.expect('{');
                        while (true) { // t -> {st -> [vals]}
                            c.ws();
                            if (c.peek() == '}') { c.expect('}'); break; }
                            int tt = c.ikey();
                            c.expect(':');
                            c.expect('{');
                            while (true) {
                                c.ws();
                                if (c.peek() == '}') { c.expect('}'); break; }
                                int st = c.ikey();
                                c.expect(':');
                                c.expect('[');
                                int64_t off = (int64_t)rvals.size();
                                int len = 0;
                                while (true) {
                                    c.ws();
                                    if (c.peek() == ']') { c.expect(']'); break; }
                                    rvals.push_back((float)c.num());
                                    ++len;
                                    c.ws();
                                    if (c.peek() == ',') c.expect(',');
                                }
                                rent.push_back({st - 1, tt - 1, off, len});
                                c.ws();
                                if (c.peek() == ',') c.expect(',');
                            }
                            c.ws();
                            if (c.peek() == ',') c.expect(',');
                        }
                    }
                    else c.skipValue();
                    c.ws();
                    if (c.peek() == ',') c.expect(',');
                }
                // finalize workload: sort by st, emit flat triples
                wlOff.emplace_back(itmax, -1);
                wlCnt.emplace_back(itmax, 0);
                std::sort(went.begin(), went.end(), [](const WEnt& a, const WEnt& b) { return a.st < b.st; });
                for (auto& e : went) {
                    int st = e.st;
                    if (st < 0 || st >= itmax) continue;
                    if (wlOff[id][st] < 0) { wlOff[id][st] = (int64_t)wlC.size(); wlCnt[id][st] = 0; }
                    wlC.push_back((uint8_t)e.c);
                    wlT.push_back((uint16_t)e.t);
                    wlV.push_back(e.v);
                    wlCnt[id][st]++;
                }
                for (int s = 0; s < itmax; ++s) if (wlOff[id][s] < 0) { wlOff[id][s] = (int64_t)wlC.size(); wlCnt[id][s] = 0; }
                // finalize risk: sort by (st, t), build contiguous blocks in riskAll
                std::sort(rent.begin(), rent.end(), [](const REnt& a, const REnt& b) { return a.st < b.st || (a.st == b.st && a.t < b.t); });
                riskBlk.emplace_back(itmax, -1);
                m1V.emplace_back(itmax, 0.0);
                size_t pos = 0;
                while (pos < rent.size()) {
                    int st = rent[pos].st;
                    if (st < 0 || st >= itmax) { ++pos; continue; }
                    int dur = (st < (int)deltaRaw[id].size()) ? deltaRaw[id][st] : 0;
                    int64_t blk = (int64_t)riskAll.size();
                    riskBlk[id][st] = blk;
                    while (pos < rent.size() && rent[pos].st == st) {
                        int t = rent[pos].t;
                        if (t < st || t >= st + dur) { ++pos; continue; }
                        for (int k = 0; k < rent[pos].len; ++k) riskAll.push_back(rvals[rent[pos].off + k]);
                        ++pos;
                    }
                }
                for (int s = 0; s < itmax; ++s) if (riskBlk[id][s] < 0) riskBlk[id][s] = (int64_t)riskAll.size();
                c.ws();
                if (c.peek() == ',') c.expect(',');
            }
        }
        else if (key == "Exclusions") {
            c.expect('{');
            while (true) {
                c.ws();
                if (c.peek() == '}') { c.expect('}'); break; }
                c.str(); // exclusion name, unused
                c.expect(':');
                c.expect('[');
                std::string i1 = c.str(); c.expect(',');
                std::string i2 = c.str(); c.expect(',');
                std::string sn = c.str();
                c.ws();
                while (c.peek() != ']' && c.peek() != 0) { c.expect(','); c.skipValue(); }
                c.expect(']');
                pendExcl.push_back({i1, i2, sn});
                c.ws();
                if (c.peek() == ',') c.expect(',');
            }
        }
        else c.skipValue();
        c.ws();
        if (c.peek() == ',') c.expect(',');
    }
    N = (int)inames.size();
    scenCum.assign(T + 1, 0);
    for (int t = 0; t < T; ++t) scenCum[t + 1] = scenCum[t] + scen[t];
    // T and Scenarios_number appear after Interventions in the schema: finalize now
    deltaV.assign(N, std::vector<int>(T, 0));
    for (int i = 0; i < N; ++i)
        for (int t = 0; t < T && t < (int)deltaRaw[i].size(); ++t) deltaV[i][t] = deltaRaw[i][t];
    deltaRaw.clear();
    deltaRaw.shrink_to_fit();
    // m1V[i][st] = sum over window of per-t mean risk (needs scen, hence deferred)
    // qexV[i][st] = sum over window of standalone excess max(0, Q_tau(own risk) - mean)
    qexV.assign(N, {});
    std::vector<double> qscratch;
    {
        int mx = 0;
        for (int t = 0; t < T; ++t) mx = std::max(mx, scen[t]);
        qscratch.assign(mx, 0.0);
    }
    for (int i = 0; i < N; ++i) {
        qexV[i].assign(tmaxV[i], 0.0);
        for (int st = 0; st < tmaxV[i]; ++st) {
            int dur = deltaV[i][st];
            if (dur <= 0) continue;
            int64_t blk = riskBlk[i][st];
            double m1 = 0.0, qx = 0.0;
            for (int t = st; t < st + dur && t < T; ++t) {
                int64_t off = blk + (scenCum[t] - scenCum[st]);
                int n = scen[t];
                if (n <= 0) continue;
                double s = 0;
                for (int k = 0; k < n; ++k) { double v = riskAll[off + k]; s += v; qscratch[k] = v; }
                double mean = s / n;
                m1 += mean;
                int kq = (int)std::ceil(n * tauQ) - 1;
                std::nth_element(qscratch.begin(), qscratch.begin() + kq, qscratch.begin() + n);
                qx += std::max(0.0, qscratch[kq] - mean);
            }
            m1V[i][st] = m1;
            qexV[i][st] = qx;
        }
    }
    nWords = (T + 63) / 64;
    seasonMask.assign(seasonNames.size(), std::vector<uint64_t>(nWords, 0));
    for (size_t s = 0; s < seasonNames.size(); ++s)
        for (int t : seasonTimes[s])
            if (t >= 1 && t <= T) seasonMask[s][(t - 1) >> 6] |= (1ULL << ((t - 1) & 63));
}

// ---------------- state ----------------
struct State {
    std::vector<int> start;
    std::vector<std::vector<double>> usage;
    std::vector<double> riskSum;
    std::vector<double> sumT;
    std::vector<double> meanT, qT;
    double obj1 = 0, obj2 = 0;
    double resVio = 0;
    long exclVio = 0;
};

static std::vector<double> scratch;

static inline double violOf(double usage, double lo, double hi) {
    double v = 0;
    if (usage > hi) v += usage - hi;
    if (usage < lo) v += lo - usage;
    return v;
}

static void applyUsageDelta(State& st, int i, int st0, double sign, double& dv) {
    int64_t off = wlOff[i][st0];
    int cnt = wlCnt[i][st0];
    for (int k = 0; k < cnt; ++k) {
        int c = wlC[off + k];
        int t = wlT[off + k];
        double old = violOf(st.usage[c][t], umin[c][t], umax[c][t]);
        st.usage[c][t] += sign * wlV[off + k];
        double nw = violOf(st.usage[c][t], umin[c][t], umax[c][t]);
        dv += nw - old;
    }
}

static void applyRisk(State& st, int i, int st0, double sign) {
    int dur = deltaV[i][st0];
    int64_t blk = riskBlk[i][st0];
    for (int t = st0; t < st0 + dur; ++t) {
        int64_t off = blk + (scenCum[t] - scenCum[st0]);
        int64_t base = scenCum[t];
        int n = scen[t];
        const float* src = &riskAll[off];
        double s = 0;
        if (sign > 0) { for (int k = 0; k < n; ++k) { double v = src[k]; st.riskSum[base + k] += v; s += v; } }
        else { for (int k = 0; k < n; ++k) { double v = src[k]; st.riskSum[base + k] -= v; s += v; } }
        st.sumT[t] += sign * s;
    }
}

static inline double tObjPart(const State& st, int t) {
    return alphaP * st.meanT[t] + (1 - alphaP) * std::max(0.0, st.qT[t] - st.meanT[t]);
}

static void recomputeTQ(State& st, int t) {
    int n = scen[t];
    int64_t base = scenCum[t];
    std::copy(st.riskSum.begin() + base, st.riskSum.begin() + base + n, scratch.begin());
    int k = qidx(t);
    std::nth_element(scratch.begin(), scratch.begin() + k, scratch.begin() + n);
    st.meanT[t] = st.sumT[t] / n;
    st.qT[t] = scratch[k];
}

static long exclOverlap(int st0, int dur, int oStart, int oDur, int sid) {
    int lo = std::max(st0, oStart);
    int hi = std::min(st0 + dur, oStart + oDur);
    if (lo >= hi) return 0;
    const std::vector<uint64_t>& m = seasonMask[sid];
    long cnt = 0;
    for (int w = lo >> 6; w <= (hi - 1) >> 6; ++w) {
        int wl = w << 6;
        int a = std::max(lo, wl), b = std::min(hi, wl + 64);
        uint64_t rm;
        if (b - wl >= 64) rm = ~0ULL; else rm = (1ULL << (b - wl)) - 1;
        rm &= ~((1ULL << (a - wl)) - 1);
        cnt += __builtin_popcountll(m[w] & rm);
    }
    return cnt;
}

// ---------------- worker: greedy + simulated annealing ----------------
struct WorkerRes {
    std::vector<int> start;
    double pen = 1e300;
    double feasObj = 1e300;
    bool hasFeas = false;
    long iters = 0;
};

static WorkerRes runWorker(int tid, unsigned long long seed, double deadline, double t0mul, double greedyNoise) {
    std::mt19937_64 rng(seed);
    std::vector<double> scratchL(*std::max_element(scen.begin(), scen.end()), 0.0);
    auto recomputeTQL = [&](State& st, int t) {
        int n = scen[t];
        int64_t base = scenCum[t];
        std::copy(st.riskSum.begin() + base, st.riskSum.begin() + base + n, scratchL.begin());
        int k = qidx(t);
        std::nth_element(scratchL.begin(), scratchL.begin() + k, scratchL.begin() + n);
        st.meanT[t] = st.sumT[t] / n;
        st.qT[t] = scratchL[k];
    };

    State cur;
    std::vector<int> order(N);
    std::vector<char> placed(N, 0);
    // construct: greedy + reinsertion repair + risk build; fills cur, returns objSumT
    auto construct = [&](double noise, bool shuffleOrder) -> double {
        placed.assign(N, 0);
        cur.start.assign(N, 0);
        cur.usage.assign(resNames.size(), std::vector<double>(T, 0.0));
        cur.riskSum.assign(scenCum[T], 0.0);
        cur.sumT.assign(T, 0.0);
        cur.meanT.assign(T, 0.0);
        cur.qT.assign(T, 0.0);

        // greedy: earliest tmax first, with per-worker noise for diversity
        for (int i = 0; i < N; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (tmaxV[a] != tmaxV[b]) return tmaxV[a] < tmaxV[b];
            return a < b;
        });
        if (shuffleOrder) {
            for (int k = N - 1; k > 0; --k) {
                if (rng() % 3 == 0) { int j = (int)(rng() % (k + 1)); std::swap(order[k], order[j]); }
            }
        }
        const double WRES_G = 1e7, WEXC_G = 1e6;
        for (int oi = 0; oi < N; ++oi) {
            int i = order[oi];
            int tm = tmaxV[i];
            double best = 1e300;
            int bestSt = 0;
            for (int st0 = 0; st0 < tm; ++st0) {
                int dur = deltaV[i][st0];
                double cost = (alphaP * m1V[i][st0] + (1 - alphaP) * qexV[i][st0]) / T;
                int64_t off = wlOff[i][st0];
                int cnt = wlCnt[i][st0];
                double dv = 0;
                for (int k = 0; k < cnt; ++k) {
                    int c = wlC[off + k], t = wlT[off + k];
                    dv += violOf(cur.usage[c][t] + wlV[off + k], umin[c][t], umax[c][t]) - violOf(cur.usage[c][t], umin[c][t], umax[c][t]);
                }
                long deCount = 0;
                for (auto& pr : excl[i]) {
                    int j = pr.first;
                    if (!placed[j]) continue;
                    deCount += exclOverlap(st0, dur, cur.start[j], deltaV[j][cur.start[j]], pr.second);
                }
                cost += WRES_G * dv + WEXC_G * (double)deCount;
                cost += noise * (double)(rng() % 1000000);
                if (cost < best) { best = cost; bestSt = st0; }
            }
            cur.start[i] = bestSt;
            double dtmp = 0;
            applyUsageDelta(cur, i, bestSt, +1.0, dtmp);
            placed[i] = 1;
        }

        // ---- violation-focused reinsertion descent (usage + exclusion only) ----
        {
            double objW = 0.01; // small pull toward low-risk starts during repair
            for (int pass = 0; pass < 80; ++pass) {
                bool improved = false;
                for (int k = N - 1; k > 0; --k) { int j = (int)(rng() % (k + 1)); std::swap(order[k], order[j]); }
                for (int oi = 0; oi < N; ++oi) {
                    int i = order[oi];
                    int tm = tmaxV[i];
                    if (tm <= 1) continue;
                    int a = cur.start[i];
                    double dtmp = 0;
                    applyUsageDelta(cur, i, a, -1.0, dtmp); // temporarily remove
                    double best = 1e300;
                    int bestSt = a;
                    double bestDv = 0;
                    long bestDe = 0;
                    for (int st0 = 0; st0 < tm; ++st0) {
                        int dur = deltaV[i][st0];
                        double dv = 0;
                        int64_t off = wlOff[i][st0];
                        int cnt = wlCnt[i][st0];
                        for (int k2 = 0; k2 < cnt; ++k2) {
                            int c = wlC[off + k2], t = wlT[off + k2];
                            dv += violOf(cur.usage[c][t] + wlV[off + k2], umin[c][t], umax[c][t]) - violOf(cur.usage[c][t], umin[c][t], umax[c][t]);
                        }
                        long de = 0;
                        for (auto& pr : excl[i]) {
                            int j = pr.first;
                            if (j == i) continue;
                            if (j < 0) continue;
                            de += exclOverlap(st0, dur, cur.start[j], deltaV[j][cur.start[j]], pr.second);
                        }
                        // note: j==i excluded; partner j may equal i? no self-exclusions assumed
                        double cost = 1e7 * dv + 1e6 * (double)de + objW * (alphaP * m1V[i][st0] + (1 - alphaP) * qexV[i][st0]) / T;
                        if (cost < best) { best = cost; bestSt = st0; bestDv = dv; bestDe = de; }
                    }
                    if (bestSt != a) improved = true;
                    cur.start[i] = bestSt;
                    double dtmp2 = 0;
                    applyUsageDelta(cur, i, bestSt, +1.0, dtmp2);
                }
                if (!improved) break;
                if (now_s() > deadline) break;
            }
        }
        // build risk state from final placement
        for (int i = 0; i < N; ++i) applyRisk(cur, i, cur.start[i], +1.0);
        for (int t = 0; t < T; ++t) recomputeTQL(cur, t);
        double objSumT_ = 0;
        for (int t = 0; t < T; ++t) objSumT_ += tObjPart(cur, t);
        cur.resVio = 0;
        for (size_t c = 0; c < resNames.size(); ++c)
            for (int t = 0; t < T; ++t) cur.resVio += violOf(cur.usage[c][t], umin[c][t], umax[c][t]);
        cur.exclVio = 0;
        for (int i = 0; i < N; ++i)
            for (auto& pr : excl[i]) if (pr.first > i)
                cur.exclVio += exclOverlap(cur.start[i], deltaV[i][cur.start[i]], cur.start[pr.first], deltaV[pr.first][cur.start[pr.first]], pr.second);

        return objSumT_;
    };
    double objSumT = construct(greedyNoise, (tid & 1) != 0);

    double objScale = std::max(1e-6, std::fabs(objSumT / T));
    double WRES = 1000.0 * objScale;
    double WEXC = 1000.0 * objScale;
    fprintf(stderr, "worker%d greedy obj=%.4f resVio=%.4g excl=%ld\n", tid, objSumT / T, cur.resVio, cur.exclVio);

    WorkerRes res;
    double bestPen = objSumT / T + WRES * cur.resVio + WEXC * (double)cur.exclVio;
    res.pen = bestPen;
    res.start = cur.start;
    if (cur.resVio <= 1e-6 && cur.exclVio == 0) { res.hasFeas = true; res.feasObj = objSumT / T; }

    double T0 = t0mul * objScale;
    double T1 = 1e-7 * objScale;
    double saStart = now_s();
    if (deadline <= saStart) return res;
    std::uniform_real_distribution<double> U(0.0, 1.0);
    // violation-targeted sampling state
    std::vector<int> violList;
    long lastViolScan = 0;
    long lastPenImprove = 0;
    long lastRestartIter = 0;
    double lastAdoptT = 0;
    long lastSeenGen = 0;
    auto rebuildViolList = [&]() {
        violList.clear();
        if (cur.resVio > 1e-9) {
            // violated cells
            std::vector<std::vector<char>> vcell(resNames.size(), std::vector<char>(T, 0));
            for (size_t c = 0; c < resNames.size(); ++c)
                for (int t = 0; t < T; ++t)
                    if (violOf(cur.usage[c][t], umin[c][t], umax[c][t]) > 1e-9) vcell[c][t] = 1;
            for (int i = 0; i < N; ++i) {
                int64_t off = wlOff[i][cur.start[i]];
                int cnt = wlCnt[i][cur.start[i]];
                for (int k = 0; k < cnt; ++k)
                    if (vcell[wlC[off + k]][wlT[off + k]]) { violList.push_back(i); break; }
            }
        }
        if (cur.exclVio > 0) {
            for (int i = 0; i < N; ++i)
                for (auto& pr : excl[i])
                    if (pr.first > i && exclOverlap(cur.start[i], deltaV[i][cur.start[i]], cur.start[pr.first], deltaV[pr.first][cur.start[pr.first]], pr.second) > 0) {
                        violList.push_back(i);
                        violList.push_back(pr.first);
                    }
        }
        std::sort(violList.begin(), violList.end());
        violList.erase(std::unique(violList.begin(), violList.end()), violList.end());
    };

    // generalized move primitives (shared affT/oldPart buffers)
    std::vector<int> affT;
    std::vector<double> oldPart;
    auto evalMoveG = [&](int i, int a, int b, double& dPen, double& dObjSum, double& dv, long& de) {
        int durA = deltaV[i][a], durB = deltaV[i][b];
        de = 0;
        for (auto& pr : excl[i]) {
            int j = pr.first;
            de -= exclOverlap(a, durA, cur.start[j], deltaV[j][cur.start[j]], pr.second);
            de += exclOverlap(b, durB, cur.start[j], deltaV[j][cur.start[j]], pr.second);
        }
        dv = 0;
        applyUsageDelta(cur, i, a, -1.0, dv);
        applyUsageDelta(cur, i, b, +1.0, dv);
        affT.clear();
        oldPart.clear();
        applyRisk(cur, i, a, -1.0);
        applyRisk(cur, i, b, +1.0);
        for (int t = a; t < a + durA; ++t) affT.push_back(t);
        for (int t = b; t < b + durB; ++t) affT.push_back(t);
        std::sort(affT.begin(), affT.end());
        affT.erase(std::unique(affT.begin(), affT.end()), affT.end());
        for (int t : affT) oldPart.push_back(tObjPart(cur, t));
        dObjSum = 0;
        int idx2 = 0;
        for (int t : affT) {
            double oldp = oldPart[idx2++];
            recomputeTQL(cur, t);
            dObjSum += tObjPart(cur, t) - oldp;
        }
        dPen = dObjSum / T + WRES * dv + WEXC * (double)de;
    };
    auto revertMoveG = [&](int i, int a, int b) {
        applyRisk(cur, i, b, -1.0);
        applyRisk(cur, i, a, +1.0);
        for (int t : affT) recomputeTQL(cur, t);
        double dv2 = 0;
        applyUsageDelta(cur, i, b, -1.0, dv2);
        applyUsageDelta(cur, i, a, +1.0, dv2);
    };
    auto updateBest = [&]() {
        double pen = objSumT / T + WRES * cur.resVio + WEXC * (double)cur.exclVio;
        if (pen < res.pen) { res.pen = pen; lastPenImprove = res.iters; if (!res.hasFeas) res.start = cur.start; }
        if (cur.resVio <= 1e-6 && cur.exclVio == 0) {
            double o = objSumT / T;
            if (o < res.feasObj) { res.feasObj = o; res.hasFeas = true; res.start = cur.start; }
            if (o < gBestObj.load(std::memory_order_relaxed)) {
                std::lock_guard<std::mutex> lk(gBestMtx);
                if (o < gBestObj.load()) { gBestObj.store(o); gBestStart = cur.start; gBestGen.fetch_add(1); }
            }
        }
    };
    auto keepMoveG = [&](int i, int b, double dObjSum, double dv, long de) {
        cur.resVio += dv;
        cur.exclVio += de;
        objSumT += dObjSum;
        cur.start[i] = b;
        updateBest();
    };
    auto undoKeepG = [&](int i, int a, int b, double dObjSum, double dv, long de) {
        revertMoveG(i, a, b);
        cur.resVio -= dv;
        cur.exclVio -= de;
        objSumT -= dObjSum;
        cur.start[i] = a;
    };
    auto worstCell = [&](int& cOut, int& tOut, double& vOut) {
        vOut = 0; cOut = -1; tOut = -1;
        for (size_t c = 0; c < resNames.size(); ++c)
            for (int t = 0; t < T; ++t) {
                double v = violOf(cur.usage[c][t], umin[c][t], umax[c][t]);
                if (v > vOut) { vOut = v; cOut = (int)c; tOut = t; }
            }
    };
    auto coveringAt = [&](int c, int t, std::vector<int>& out) {
        out.clear();
        for (int i = 0; i < N; ++i) {
            int64_t off = wlOff[i][cur.start[i]];
            int cnt = wlCnt[i][cur.start[i]];
            for (int k = 0; k < cnt; ++k)
                if (wlC[off + k] == c && wlT[off + k] == t && wlV[off + k] > 1e-12) { out.push_back(i); break; }
        }
    };
    // ruin & recreate: remove victims near a (resource,time) center, reinsert greedily
    int lnsFails = 0; // consecutive rejected targeted LNS rounds -> escalate size/radius
    auto ruinRecreate = [&](bool targetViol) {
        int cC, tC;
        if (targetViol) { double vC; worstCell(cC, tC, vC); if (vC <= 1e-9 || cC < 0) return; }
        else {
            // objective-directed: sample t with probability ~ tObjPart (high-cost periods)
            double tot = 0;
            for (int t = 0; t < T; ++t) tot += tObjPart(cur, t);
            double r = U(rng) * tot;
            tC = T - 1;
            double acc2 = 0;
            for (int t = 0; t < T; ++t) { acc2 += tObjPart(cur, t); if (acc2 >= r) { tC = t; break; } }
            cC = (int)(rng() % resNames.size());
        }
        int radius = targetViol ? 3 + std::min(lnsFails, 4) : 3;
        int cap = targetViol ? std::min(12 + lnsFails * 4, 28) : 12;
        std::vector<int> victims;
        for (int i = 0; i < N; ++i) {
            if (cur.start[i] < 0) continue;
            int64_t off = wlOff[i][cur.start[i]];
            int cnt = wlCnt[i][cur.start[i]];
            for (int k = 0; k < cnt; ++k)
                if (wlC[off + k] == cC && std::abs((int)wlT[off + k] - tC) <= radius) { victims.push_back(i); break; }
        }
        if (victims.size() < 2) { if (targetViol) ++lnsFails; return; }
        if ((int)victims.size() > cap) { std::shuffle(victims.begin(), victims.end(), rng); victims.resize(cap); }
        std::shuffle(victims.begin(), victims.end(), rng); // reinsertion order; must precede oldSt capture
        int nv = (int)victims.size();
        std::vector<int> oldSt(nv);
        double savedObj = objSumT, savedRV = cur.resVio;
        long savedEX = cur.exclVio;
        // old windows union
        std::vector<int> affOld;
        for (int k = 0; k < nv; ++k) {
            int i = victims[k];
            oldSt[k] = cur.start[i];
            int dur = deltaV[i][oldSt[k]];
            for (int t = oldSt[k]; t < oldSt[k] + dur; ++t) affOld.push_back(t);
        }
        std::sort(affOld.begin(), affOld.end());
        affOld.erase(std::unique(affOld.begin(), affOld.end()), affOld.end());
        std::vector<double> oldP;
        for (int t : affOld) oldP.push_back(tObjPart(cur, t));
        for (int t : affOld) objSumT -= tObjPart(cur, t);
        double penBefore = savedObj / T + WRES * savedRV + WEXC * (double)savedEX;
        // remove all victims
        for (int k = 0; k < nv; ++k) {
            int i = victims[k];
            double d = 0;
            applyUsageDelta(cur, i, oldSt[k], -1.0, d);
            cur.resVio += d;
            applyRisk(cur, i, oldSt[k], -1.0);
            cur.start[i] = -1;
        }
        for (int t : affOld) { recomputeTQL(cur, t); objSumT += tObjPart(cur, t); }
        // reinsert in shuffled order, greedy best start
        std::vector<int> affNew;
        for (int k = 0; k < nv; ++k) {
            int i = victims[k];
            int tm = tmaxV[i];
            double best = 1e300;
            int bestSt = 0;
            double bestDv = 0;
            for (int st0 = 0; st0 < tm; ++st0) {
                int dur = deltaV[i][st0];
                double dv = 0;
                int64_t off = wlOff[i][st0];
                int cnt = wlCnt[i][st0];
                for (int k2 = 0; k2 < cnt; ++k2) {
                    int c = wlC[off + k2], t = wlT[off + k2];
                    dv += violOf(cur.usage[c][t] + wlV[off + k2], umin[c][t], umax[c][t]) - violOf(cur.usage[c][t], umin[c][t], umax[c][t]);
                }
                long de = 0;
                for (auto& pr : excl[i]) {
                    int j = pr.first;
                    if (cur.start[j] < 0) continue;
                    de += exclOverlap(st0, dur, cur.start[j], deltaV[j][cur.start[j]], pr.second);
                }
                double cost = 1e7 * dv + 1e6 * (double)de + 0.01 * (alphaP * m1V[i][st0] + (1 - alphaP) * qexV[i][st0]) / T;
                if (cost < best) { best = cost; bestSt = st0; bestDv = dv; }
            }
            cur.start[i] = bestSt;
            double d = 0;
            applyUsageDelta(cur, i, bestSt, +1.0, d);
            cur.resVio += d;
            // risk insert with obj delta accounting
            int dur = deltaV[i][bestSt];
            std::vector<int> wts;
            for (int t = bestSt; t < bestSt + dur; ++t) { wts.push_back(t); affNew.push_back(t); }
            std::vector<double> op;
            for (int t : wts) op.push_back(tObjPart(cur, t));
            applyRisk(cur, i, bestSt, +1.0);
            int xi = 0;
            for (int t : wts) { double op2 = op[xi++]; recomputeTQL(cur, t); objSumT += tObjPart(cur, t) - op2; }
        }
        // exclusion total recompute (victim pairs may double count otherwise)
        long ex2 = 0;
        for (int i = 0; i < N; ++i)
            for (auto& pr : excl[i]) if (pr.first > i)
                ex2 += exclOverlap(cur.start[i], deltaV[i][cur.start[i]], cur.start[pr.first], deltaV[pr.first][cur.start[pr.first]], pr.second);
        cur.exclVio = ex2;
        double penAfter = objSumT / T + WRES * cur.resVio + WEXC * (double)cur.exclVio;
        double frac2 = (now_s() - saStart) / std::max(1e-9, deadline - saStart);
        double Tk2 = T0 * std::pow(T1 / T0, frac2);
        bool acc = (penAfter <= penBefore) || (U(rng) < std::exp(-(penAfter - penBefore) / std::max(Tk2, 1e-300)));
        if (acc) {
            updateBest();
            lnsFails = 0;
        } else {
            if (targetViol) ++lnsFails;
            // full revert: remove from current, reinsert at old
            for (int k = 0; k < nv; ++k) {
                int i = victims[k];
                int a = cur.start[i];
                double d = 0;
                applyUsageDelta(cur, i, a, -1.0, d);
                applyRisk(cur, i, a, -1.0);
                cur.start[i] = -1;
            }
            for (int k = 0; k < nv; ++k) {
                int i = victims[k];
                cur.start[i] = oldSt[k];
                double d = 0;
                applyUsageDelta(cur, i, oldSt[k], +1.0, d);
                applyRisk(cur, i, oldSt[k], +1.0);
            }
            for (int t : affOld) recomputeTQL(cur, t);
            for (int t : affNew) recomputeTQL(cur, t);
            objSumT = savedObj;
            cur.resVio = savedRV;
            cur.exclVio = savedEX;
        }
    };

    while (now_s() < deadline) {
        ++res.iters;
        // adaptive weight boost / restart when stuck infeasible
        if ((res.iters & 0x3FFFF) == 0) {
            double rv = 0;
            for (size_t c = 0; c < resNames.size(); ++c)
                for (int t = 0; t < T; ++t) rv += violOf(cur.usage[c][t], umin[c][t], umax[c][t]);
            cur.resVio = rv;
            if ((rv > 1e-9 || cur.exclVio > 0) && !res.hasFeas && res.iters - lastRestartIter > (1 << 20) && now_s() < deadline - 45.0) {
                double noise2 = greedyNoise * 10 + 1e-6 * (double)(1 + (rng() % 100));
                objSumT = construct(noise2, (rng() & 1) != 0);
                objScale = std::max(1e-6, std::fabs(objSumT / T));
                WRES = 1000.0 * objScale;
                WEXC = 1000.0 * objScale;
                saStart = now_s();
                lastRestartIter = res.iters;
                lastPenImprove = res.iters;
                lastViolScan = 0;
                lnsFails = 0;
                updateBest();
                fprintf(stderr, "worker%d restart at %.0fs resVio=%.4g excl=%ld\n", tid, now_s(), cur.resVio, cur.exclVio);
                continue;
            }
            if (rv > 1e-9 || cur.exclVio > 0) {
                if (res.iters - lastPenImprove > (1 << 19)) {
                    WRES *= 2;
                    WEXC *= 2;
                    if (WRES > 1e13 * objScale) { WRES = 1e13 * objScale; WEXC = 1e13 * objScale; }
                    lastPenImprove = res.iters;
                }
            }
            // migration: adopt global best feasible when clearly better than local best
            double gb = gBestObj.load(std::memory_order_relaxed);
            long gg = gBestGen.load(std::memory_order_relaxed);
            if (res.hasFeas && gg != lastSeenGen && gb < res.feasObj - 0.005 * std::fabs(res.feasObj)
                && now_s() - lastAdoptT > 25.0 && now_s() < deadline - 60.0) {
                std::vector<int> snap;
                { std::lock_guard<std::mutex> lk(gBestMtx); snap = gBestStart; }
                lastSeenGen = gg;
                if ((int)snap.size() == N) {
                    for (auto& row : cur.usage) std::fill(row.begin(), row.end(), 0.0);
                    std::fill(cur.riskSum.begin(), cur.riskSum.end(), 0.0);
                    std::fill(cur.sumT.begin(), cur.sumT.end(), 0.0);
                    cur.start = snap;
                    double dvA = 0;
                    for (int i2 = 0; i2 < N; ++i2) {
                        double d = 0;
                        applyUsageDelta(cur, i2, snap[i2], +1.0, d);
                        dvA += d;
                        applyRisk(cur, i2, snap[i2], +1.0);
                    }
                    objSumT = 0;
                    for (int t = 0; t < T; ++t) { recomputeTQL(cur, t); objSumT += tObjPart(cur, t); }
                    cur.resVio = dvA;
                    long exA = 0;
                    for (int i2 = 0; i2 < N; ++i2)
                        for (auto& pr : excl[i2]) if (pr.first > i2)
                            exA += exclOverlap(cur.start[i2], deltaV[i2][cur.start[i2]], cur.start[pr.first], deltaV[pr.first][cur.start[pr.first]], pr.second);
                    cur.exclVio = exA;
                    res.feasObj = objSumT / T;
                    res.start = snap;
                    lastAdoptT = now_s();
                }
            }
        }
        int i;
        bool infeas = (cur.resVio > 1e-9 || cur.exclVio > 0);
        if (infeas) {
            if (res.iters - lastViolScan > 4096) { rebuildViolList(); lastViolScan = res.iters; }
            if (!violList.empty() && U(rng) < 0.75) i = violList[rng() % violList.size()];
            else i = (int)(rng() % N);
        } else {
            i = (int)(rng() % N);
        }
        int tm = tmaxV[i];
        if (tm <= 1) continue;
        int a = cur.start[i];
        double frac = (now_s() - saStart) / (deadline - saStart);
        double Tk = T0 * std::pow(T1 / T0, frac);
        bool repairPhase = infeas && frac > 0.60;
        double roll = U(rng);

        double lnsP = infeas ? 0.12 : 0.05;
        if (roll < lnsP) { // ruin & recreate (targets worst violated cell when infeasible)
            ruinRecreate(infeas);
            continue;
        }
        if (infeas && roll < lnsP + 0.04) {
            // ---- targeted 2-move: eject from worst violated cell ----
            int cS, tS;
            double vS;
            worstCell(cS, tS, vS);
            std::vector<int> cov;
            if (vS > 1e-9) coveringAt(cS, tS, cov);
            if (!cov.empty()) {
                int i2 = cov[rng() % cov.size()];
                int a2 = cur.start[i2];
                int tm2 = tmaxV[i2];
                if (tm2 > 1) {
                    double bestDP = 1e300, bdO = 0, bdV = 0;
                    long bdE = 0;
                    int bestB = a2;
                    for (int b = 0; b < tm2; ++b) {
                        if (b == a2) continue;
                        double dPen, dObjSum, dv;
                        long de;
                        evalMoveG(i2, a2, b, dPen, dObjSum, dv, de);
                        revertMoveG(i2, a2, b);
                        if (dPen < bestDP) { bestDP = dPen; bestB = b; bdO = dObjSum; bdV = dv; bdE = de; }
                    }
                    if (bestB != a2) {
                        double dPen1, dObj1, dv1;
                        long de1;
                        evalMoveG(i2, a2, bestB, dPen1, dObj1, dv1, de1);
                        keepMoveG(i2, bestB, dObj1, dv1, de1);
                        int c2, t2;
                        double v2;
                        worstCell(c2, t2, v2);
                        std::vector<int> cov2;
                        if (v2 > 1e-9) coveringAt(c2, t2, cov2);
                        double dPen2 = 0, dObj2 = 0, dv2 = 0;
                        long de2 = 0;
                        int j2 = -1, bj2 = -1, aj2 = -1;
                        cov2.erase(std::remove(cov2.begin(), cov2.end(), i2), cov2.end());
                        if (!cov2.empty()) {
                            j2 = cov2[rng() % cov2.size()];
                            aj2 = cur.start[j2];
                            int tmj = tmaxV[j2];
                            if (tmj > 1) {
                                double best2 = 1e300;
                                for (int b = 0; b < tmj; ++b) {
                                    if (b == aj2) continue;
                                    double dPen, dObjSum, dv;
                                    long de;
                                    evalMoveG(j2, aj2, b, dPen, dObjSum, dv, de);
                                    revertMoveG(j2, aj2, b);
                                    if (dPen < best2) { best2 = dPen; bj2 = b; dObj2 = dObjSum; dv2 = dv; de2 = de; }
                                }
                                if (bj2 >= 0) {
                                    evalMoveG(j2, aj2, bj2, dPen2, dObj2, dv2, de2);
                                    keepMoveG(j2, bj2, dObj2, dv2, de2);
                                }
                            }
                        }
                        double total = dPen1 + dPen2;
                        bool acc = (total < 0) || (U(rng) < std::exp(-total / std::max(Tk, 1e-300)));
                        if (!acc) {
                            if (j2 >= 0 && bj2 >= 0) undoKeepG(j2, aj2, bj2, dObj2, dv2, de2);
                            undoKeepG(i2, a2, bestB, dObj1, dv1, de1);
                            // exact repair: cells may have been recomputed with tentative co-moves
                            affT.clear();
                            for (int t = a2; t < a2 + deltaV[i2][a2]; ++t) affT.push_back(t);
                            for (int t = bestB; t < bestB + deltaV[i2][bestB]; ++t) affT.push_back(t);
                            if (j2 >= 0 && bj2 >= 0) {
                                for (int t = aj2; t < aj2 + deltaV[j2][aj2]; ++t) affT.push_back(t);
                                for (int t = bj2; t < bj2 + deltaV[j2][bj2]; ++t) affT.push_back(t);
                            }
                            std::sort(affT.begin(), affT.end());
                            affT.erase(std::unique(affT.begin(), affT.end()), affT.end());
                            for (int t : affT) recomputeTQL(cur, t);
                        }
                    }
                }
            }
            continue;
        }
        if (repairPhase && !violList.empty() && roll < 0.22) {
            // full start scan for sampled intervention
            double bestDP = 1e300, bdO = 0, bdV = 0;
            long bdE = 0;
            int bestB = a;
            for (int b = 0; b < tm; ++b) {
                if (b == a) continue;
                double dPen, dObjSum, dv;
                long de;
                evalMoveG(i, a, b, dPen, dObjSum, dv, de);
                revertMoveG(i, a, b);
                if (dPen < bestDP) { bestDP = dPen; bestB = b; bdO = dObjSum; bdV = dv; bdE = de; }
            }
            if (bestB != a && bestDP < -1e-12) {
                double dPen, dObjSum, dv;
                long de;
                evalMoveG(i, a, bestB, dPen, dObjSum, dv, de);
                keepMoveG(i, bestB, dObjSum, dv, de);
            }
            continue;
        }
        if (!infeas && frac > 0.85 && roll >= lnsP && roll < lnsP + 0.18) {
            // final intensification: full start scan, take best improving move
            double bestDP = 1e300, bdO = 0, bdV = 0;
            long bdE = 0;
            int bestB = a;
            for (int b2 = 0; b2 < tm; ++b2) {
                if (b2 == a) continue;
                double dPen, dObjSum, dv;
                long de;
                evalMoveG(i, a, b2, dPen, dObjSum, dv, de);
                revertMoveG(i, a, b2);
                if (dPen < bestDP) { bestDP = dPen; bestB = b2; bdO = dObjSum; bdV = dv; bdE = de; }
            }
            if (bestB != a && bestDP < -1e-12) {
                double dPen, dObjSum, dv;
                long de;
                evalMoveG(i, a, bestB, dPen, dObjSum, dv, de);
                keepMoveG(i, bestB, dObjSum, dv, de);
            }
            continue;
        }
        if (roll >= 0.27 && roll < 0.38) {
            // pairwise start swap (skip pairs sharing an exclusion edge: de would use stale starts)
            int j = (int)(rng() % N);
            if (j != i) {
                int ai = cur.start[i], aj = cur.start[j];
                if (aj != ai && aj < tmaxV[i] && ai < tmaxV[j]) {
                    bool edge = false;
                    for (auto& pr : excl[i]) if (pr.first == j) { edge = true; break; }
                    if (!edge) {
                        double dP1, dO1, dv1, dP2, dO2, dv2;
                        long dE1, dE2;
                        evalMoveG(i, ai, aj, dP1, dO1, dv1, dE1);
                        evalMoveG(j, aj, ai, dP2, dO2, dv2, dE2);
                        double tot = dP1 + dP2;
                        bool acc;
                        if (infeas) {
                            double dvio = (dv1 + dv2) + (double)(dE1 + dE2);
                            double TkF = 0.05 * std::pow(2e-4, frac);
                            if (dvio < -1e-12) acc = true;
                            else if (dvio <= 1e-12) acc = (dO1 + dO2 <= 0) || (U(rng) < 0.5);
                            else acc = (U(rng) < std::exp(-dvio / std::max(TkF, 1e-300)));
                        } else {
                            acc = (tot <= 0) || (U(rng) < std::exp(-tot / std::max(Tk, 1e-300)));
                        }
                        if (acc) {
                            keepMoveG(i, aj, dO1, dv1, dE1);
                            keepMoveG(j, ai, dO2, dv2, dE2);
                        } else {
                            revertMoveG(j, aj, ai); // affT still j's
                            double dPx, dOx, dvx;
                            long dEx;
                            evalMoveG(i, aj, ai, dPx, dOx, dvx, dEx); // inverse move restores i and affT
                            // exact repair: j-window cells were recomputed while i still tentative
                            affT.clear();
                            for (int t = ai; t < ai + deltaV[i][ai]; ++t) affT.push_back(t);
                            for (int t = aj; t < aj + deltaV[i][aj]; ++t) affT.push_back(t);
                            for (int t = aj; t < aj + deltaV[j][aj]; ++t) affT.push_back(t);
                            for (int t = ai; t < ai + deltaV[j][ai]; ++t) affT.push_back(t);
                            std::sort(affT.begin(), affT.end());
                            affT.erase(std::unique(affT.begin(), affT.end()), affT.end());
                            for (int t : affT) recomputeTQL(cur, t);
                        }
                    }
                }
            }
            continue;
        }
        if (roll >= 0.38 && roll < 0.41) {
            // 3-rotation: i<-start[j], j<-start[k], k<-start[i] (no exclusion edges among the 3)
            int j = (int)(rng() % N), k2 = (int)(rng() % N);
            if (j != i && k2 != i && k2 != j) {
                int ai = cur.start[i], aj = cur.start[j], ak = cur.start[k2];
                if (aj != ai && aj < tmaxV[i] && ak < tmaxV[j] && ai < tmaxV[k2] && ak != aj && ai != ak) {
                    bool edge = false;
                    for (auto& pr : excl[i]) if (pr.first == j || pr.first == k2) { edge = true; break; }
                    if (!edge) for (auto& pr : excl[j]) if (pr.first == k2) { edge = true; break; }
                    if (!edge) {
                        double dP1, dO1, dv1, dP2, dO2, dv2, dP3, dO3, dv3;
                        long dE1, dE2, dE3;
                        evalMoveG(i, ai, aj, dP1, dO1, dv1, dE1);
                        evalMoveG(j, aj, ak, dP2, dO2, dv2, dE2);
                        evalMoveG(k2, ak, ai, dP3, dO3, dv3, dE3);
                        double tot = dP1 + dP2 + dP3;
                        bool acc;
                        if (infeas) {
                            double dvio = (dv1 + dv2 + dv3) + (double)(dE1 + dE2 + dE3);
                            double TkF = 0.05 * std::pow(2e-4, frac);
                            if (dvio < -1e-12) acc = true;
                            else if (dvio <= 1e-12) acc = (dO1 + dO2 + dO3 <= 0) || (U(rng) < 0.5);
                            else acc = (U(rng) < std::exp(-dvio / std::max(TkF, 1e-300)));
                        } else {
                            acc = (tot <= 0) || (U(rng) < std::exp(-tot / std::max(Tk, 1e-300)));
                        }
                        if (acc) {
                            keepMoveG(i, aj, dO1, dv1, dE1);
                            keepMoveG(j, ak, dO2, dv2, dE2);
                            keepMoveG(k2, ai, dO3, dv3, dE3);
                        } else {
                            revertMoveG(k2, ak, ai); // affT still k2's
                            double dPx, dOx, dvx;
                            long dEx;
                            evalMoveG(j, ak, aj, dPx, dOx, dvx, dEx); // inverse restores j, fixes affT
                            evalMoveG(i, aj, ai, dPx, dOx, dvx, dEx); // inverse restores i
                            // exact repair of cells recomputed while others still tentative
                            affT.clear();
                            for (int t = ai; t < ai + deltaV[i][ai]; ++t) affT.push_back(t);
                            for (int t = aj; t < aj + deltaV[i][aj]; ++t) affT.push_back(t);
                            for (int t = aj; t < aj + deltaV[j][aj]; ++t) affT.push_back(t);
                            for (int t = ak; t < ak + deltaV[j][ak]; ++t) affT.push_back(t);
                            for (int t = ak; t < ak + deltaV[k2][ak]; ++t) affT.push_back(t);
                            for (int t = ai; t < ai + deltaV[k2][ai]; ++t) affT.push_back(t);
                            std::sort(affT.begin(), affT.end());
                            affT.erase(std::unique(affT.begin(), affT.end()), affT.end());
                            for (int t : affT) recomputeTQL(cur, t);
                        }
                    }
                }
            }
            continue;
        }
        int b;
        if (!infeas && U(rng) < 0.55) { // local small shift (only when feasible)
            int d = (int)(rng() % 7) - 3;
            b = a + d;
            if (b < 0) b = 0;
            if (b >= tm) b = tm - 1;
        } else {
            b = (int)(rng() % tm);
        }
        if (b == a) continue;
        double dPen, dObjSum, dv;
        long de;
        evalMoveG(i, a, b, dPen, dObjSum, dv, de);
        bool accept;
        if (infeas) {
            // lexicographic: pure violation minimization while infeasible
            double dvio = dv + (double)de;
            double TkF = 0.05 * std::pow(2e-4, frac);
            if (dvio < -1e-12) accept = true;
            else if (dvio <= 1e-12) accept = (dObjSum <= 0) || (U(rng) < 0.5);
            else accept = (U(rng) < std::exp(-dvio / std::max(TkF, 1e-300)));
        } else {
            accept = (dPen <= 0) || (U(rng) < std::exp(-dPen / std::max(Tk, 1e-300)));
        }
        if (accept) {
            keepMoveG(i, b, dObjSum, dv, de);
        } else {
            revertMoveG(i, a, b);
        }
    }
    return res;
}

int main(int argc, char** argv) {
    g_t0 = Clock::now();
    std::string instPath, outPath;
    unsigned long long seed = 1;
    double budget = 800.0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--instance") instPath = argv[++i];
        else if (a == "--output") outPath = argv[++i];
        else if (a == "--seed") seed = strtoull(argv[++i], nullptr, 10);
        else if (a == "--time") budget = atof(argv[++i]);
    }
    parseInstance(instPath.c_str());
    double parseT = now_s();
    fprintf(stderr, "parse_done %.2fs N=%d T=%d R=%zu risk=%zuM\n", parseT, N, T, resNames.size(), riskAll.size() / 1000000);

    // resolve exclusions
    excl.assign(N, {});
    for (auto& pe : pendExcl) {
        auto a = iidx.find(pe.i1), b = iidx.find(pe.i2), s = seasonIdx.find(pe.season);
        if (a == iidx.end() || b == iidx.end() || s == seasonIdx.end()) continue;
        excl[a->second].push_back({b->second, s->second});
        excl[b->second].push_back({a->second, s->second});
    }

    // ---- 4-thread portfolio: greedy + SA per thread ----
    scratch.assign(*std::max_element(scen.begin(), scen.end()), 0.0);
    double deadline = std::min(budget - 8.0, budget * 0.985);
    double t0base = 0.02;
    double t0muls[4] = {1.0, 0.3, 3.0, 0.1};
    double noises[4] = {0.0, 1e-6, 1e-5, 1e-4};
    std::vector<WorkerRes> results(4);
    std::vector<std::thread> ths;
    for (int j = 0; j < 4; ++j) {
        unsigned long long s2 = seed ^ (0x9E3779B97F4A7C15ULL * (unsigned long long)(j + 1));
        ths.emplace_back([&, j, s2]() {
            results[j] = runWorker(j, s2, deadline, t0base * t0muls[j], noises[j]);
        });
    }
    for (auto& t : ths) t.join();
    long iters = 0;
    for (auto& r : results) iters += r.iters;
    // select best: feasible with lowest obj, else lowest penalized
    int bi = -1;
    double bestF = 1e300;
    for (int j = 0; j < 4; ++j)
        if (results[j].hasFeas && results[j].feasObj < bestF) { bestF = results[j].feasObj; bi = j; }
    if (bi < 0) {
        double bestP = 1e300;
        for (int j = 0; j < 4; ++j)
            if (results[j].pen < bestP) { bestP = results[j].pen; bi = j; }
    }
    std::vector<int> bestStart = results[bi].start;
    double bestFeasObj = results[bi].hasFeas ? results[bi].feasObj : 1e300;
    std::vector<int> bestFeasStart = results[bi].hasFeas ? results[bi].start : std::vector<int>();
    fprintf(stderr, "sa_done %.2fs total_iters=%ld selected_thread=%d feas=%d\n", now_s(), iters, bi, results[bi].hasFeas ? 1 : 0);

    // ---- final state selection ----
    const std::vector<int>& finalStart = bestFeasStart.empty() ? bestStart : bestFeasStart;
    // rebuild full state from finalStart for exact reporting
    State fin;
    fin.start = finalStart;
    fin.usage.assign(resNames.size(), std::vector<double>(T, 0.0));
    fin.riskSum.assign(scenCum[T], 0.0);
    fin.sumT.assign(T, 0.0);
    fin.meanT.assign(T, 0.0);
    fin.qT.assign(T, 0.0);
    for (int i = 0; i < N; ++i) {
        double dtmp = 0;
        applyUsageDelta(fin, i, fin.start[i], +1.0, dtmp);
        applyRisk(fin, i, fin.start[i], +1.0);
    }
    for (int t = 0; t < T; ++t) recomputeTQ(fin, t);
    fin.obj1 = 0;
    for (int t = 0; t < T; ++t) fin.obj1 += fin.meanT[t];
    fin.obj1 /= T;
    fin.obj2 = 0;
    for (int t = 0; t < T; ++t) fin.obj2 += std::max(0.0, fin.qT[t] - fin.meanT[t]);
    fin.obj2 /= T;
    fin.resVio = 0;
    for (size_t c = 0; c < resNames.size(); ++c)
        for (int t = 0; t < T; ++t) fin.resVio += violOf(fin.usage[c][t], umin[c][t], umax[c][t]);
    fin.exclVio = 0;
    for (int i = 0; i < N; ++i)
        for (auto& pr : excl[i]) if (pr.first > i)
            fin.exclVio += exclOverlap(fin.start[i], deltaV[i][fin.start[i]], fin.start[pr.first], deltaV[pr.first][fin.start[pr.first]], pr.second);

    // write solution
    FILE* out = fopen(outPath.c_str(), "w");
    if (!out) { fprintf(stderr, "cannot write %s\n", outPath.c_str()); exit(2); }
    for (int i = 0; i < N; ++i) fprintf(out, "%s %d\n", inames[i].c_str(), finalStart[i] + 1);
    fclose(out);

    if (fin.resVio > 1e-6 || fin.exclVio > 0) {
        for (size_t c = 0; c < resNames.size(); ++c)
            for (int t = 0; t < T; ++t) {
                double v = violOf(fin.usage[c][t], umin[c][t], umax[c][t]);
                if (v > 1e-6) fprintf(stderr, "VIOL %s t=%d usage=%.4f [%.4f,%.4f] v=%.4f\n", resNames[c].c_str(), t + 1, fin.usage[c][t], umin[c][t], umax[c][t], v);
            }
    }
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double objF = alphaP * fin.obj1 + (1 - alphaP) * fin.obj2;
    printf("{\"feasible\":%s,\"obj\":%.10g,\"obj1\":%.10g,\"obj2\":%.10g,\"resVio\":%.6g,\"exclVio\":%ld,"
           "\"parse_s\":%.2f,\"greedy_s\":%.2f,\"sa_s\":%.2f,\"total_s\":%.2f,\"iters\":%ld,\"peakMB\":%ld}\n",
           (fin.resVio <= 1e-6 && fin.exclVio == 0) ? "true" : "false",
           objF, fin.obj1, fin.obj2, fin.resVio, fin.exclVio,
           parseT, parseT, now_s(), now_s(), iters, ru.ru_maxrss / 1024);
    return 0;
}
