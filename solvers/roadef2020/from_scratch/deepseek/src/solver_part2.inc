// ------------------------------------------------------------------ solver: search part
// Included by solver.cpp
//
// Design:
//  * state = start time per intervention, resource usage U[res][t], per-period
//    scenario-risk vector R[t][s], mean[t], q[t]
//  * objective (exact, as the official checker computes it):
//        F = alpha * (1/T) sum_t mean[t] + (1-alpha) * (1/T) sum_t max(q[t]-mean[t],0)
//    mean[t] = (1/S_t) sum_s R[t][s],  q[t] = rank(ceil(S_t*quant)-1) of sorted R[t]
//  * capacity violations are handled by an adaptive penalty: G = F + lambda * V,
//        V = sum_{res,t} (max(0,U-max))^2 + (max(0,min-U))^2
//  * moves: single start change, swap, ruin&recreate, plus deterministic polish
// ------------------------------------------------------------------

struct Params {
    double timeLimit = 840.0;
    double reserve = 8.0;
    uint64_t seed = 1;
    int    candCap = 96;
    int    orderMode = 1;
    double noise = 0.30;
    int    restarts = 4;
    double pSingle = 0.75;
    double pSwap = 0.08;
    double pRuin = 0.17;
    int    ruinMin = 2;
    int    ruinMax = 6;
    double t0frac = 0.004;
    double t1frac = 0.00005;
    double polishFrac = 0.15;
    double lamInit = 0.0;
    double repairFrac = 0.20;
    int    loadPow = 4;
    double lamC = 30.0;        // construction slack-avoidance weight
    double lamScale = 20.0;    // penalty weight = lamScale * F
    double lnsFrac = 0.0;      // share of the search time spent in LNS
    int    saCycles = 1;       // number of SA reheat cycles
    double pTarget = 0.0;      // probability of a quantile-targeted move
    int    targetCand = 24;    // candidates evaluated by a targeted move
    double restartGap = 0.01;  // restart from best when F exceeds best by this fraction
    int    lnsKMin = 4;
    int    lnsKMax = 16;
    int    lnsCand = 12;       // exact candidate evaluations per reinsertion
};

struct VerifyRes {
    long long viol = 0;
    long long schedViol = 0, resViol = 0, excViol = 0;
    bool feasible = false;
    double obj1 = 0, obj2 = 0, obj = 0;
};

struct Solver {
    Instance& I;
    Params P;
    RNG rng;

    vector<int> st;
    vector<double> U;
    vector<double> R;
    vector<double> mean, qv;
    double obj1 = 0, extSum = 0;
    double V = 0;
    int nExc = 0;
    double lam = 1.0;
    double lamE = 1.0;   // penalty per exclusion violation
    int stride = 1;

    vector<int> mark, pos, idBuf;
    vector<double> dbuf;
    int stamp = 0;
    vector<double> scratch;
    vector<double> heap;

    struct PSave { int t; double m, q; };
    struct PLevel { int off; double ext, obj1, V; int nexc; };
    vector<PSave> psav;
    vector<PLevel> plev;

    vector<int> bestFeas;
    double bestFeasF = 1e300;
    vector<int> bestAny;
    double bestAnyV = 1e300;
    double bestAnyF = 1e300;

    long long iters = 0, accepted = 0, applied = 0;
    vector<pair<double, double>> trace;   // (elapsed seconds, best feasible F)
    double tZero = 0.0;
    double bestTraced = 1e300;
    long long repairPasses = 0;
    vector<int> tsBuf;
    vector<vector<int>> cand;

    Solver(Instance& inst, Params p) : I(inst), P(p), rng(p.seed) {
        stride = I.maxS;
        st.assign(I.nInt, 0);
        U.assign((size_t)I.nRes * I.T + 1, 0.0);
        R.assign((size_t)I.T * stride + stride, 0.0);
        mean.assign(I.T + 1, 0.0);
        qv.assign(I.T + 1, 0.0);
        mark.assign((size_t)I.nRes * I.T + 1, 0);
        pos.assign((size_t)I.nRes * I.T + 1, 0);
        idBuf.reserve(8192); dbuf.reserve(8192);
        scratch.assign(stride + 2, 0.0);
        heap.assign(stride + 2, 0.0);
        psav.reserve(4096);
        plev.reserve(64);
        tsBuf.reserve(4096);
        buildCandidates();
        lam = (P.lamInit > 0) ? P.lamInit : 1.0;
    }

    inline double F() const { return I.alpha * obj1 + (1.0 - I.alpha) * extSum / (double)I.T; }
    inline double G() const { return F() + lam * V + lamE * (double)nExc; }
    inline bool isFeas() const { return V <= 1e-9 && nExc == 0; }

    void buildCandidates() {
        cand.assign(I.nInt, {});
        for (int i = 0; i < I.nInt; i++) {
            int hi = I.slotHi[i];
            if (hi <= 0) continue;
            vector<int> s;
            s.reserve(hi);
            for (int stt = 1; stt <= hi; stt++) if (I.okStart[I.slot(i, stt)]) s.push_back(stt);
            int cap = P.candCap;
            if ((int)s.size() > cap) {
                vector<pair<double, int>> key;
                key.reserve(s.size());
                for (int stt : s) key.push_back({I.cArr[I.slot(i, stt)], stt});
                nth_element(key.begin(), key.begin() + cap, key.end());
                key.resize(cap);
                sort(key.begin(), key.end());
                s.clear();
                for (auto& kv : key) s.push_back(kv.second);
            } else {
                sort(s.begin(), s.end(), [&](int a, int b) { return I.cArr[I.slot(i, a)] < I.cArr[I.slot(i, b)]; });
            }
            cand[i] = std::move(s);
        }
    }

    // breakout weights (repair phase only)
    vector<double> cellW;
    bool useW = false;
    inline double penCellW(size_t id, double u) const {
        double p = 0.0;
        double ov = u - I.rmax[id];
        if (ov > 0) p += ov * ov;
        double un = I.rmin[id] - u;
        if (un > 0) p += un * un;
        if (useW) p *= cellW[id];
        return p;
    }

    inline double penCell(size_t id, double u) const {
        double p = 0.0;
        double ov = u - I.rmax[id];
        if (ov > 0) p += ov * ov;
        double un = I.rmin[id] - u;
        if (un > 0) p += un * un;
        return p;
    }

    int buildCellDelta(int i, int stOld, int stNew) {
        stamp++;
        int cnt = 0;
        if (stOld > 0) {
            int sl = I.slot(i, stOld);
            const int wst1 = stOld + I.delta[sl] - 1;
            for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1]; k++) {
                int t = I.wlT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                if (t + 1 < stOld || t + 1 > wst1) continue;
                int id = I.wlRes[k] * I.T + t;
                if (mark[id] != stamp) { mark[id] = stamp; pos[id] = cnt; idBuf.push_back(id); dbuf.push_back(-(double)I.wlVal[k]); cnt++; }
                else dbuf[pos[id]] -= (double)I.wlVal[k];
            }
        }
        if (stNew > 0) {
            int sl = I.slot(i, stNew);
            const int wst2 = stNew + I.delta[sl] - 1;
            for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1]; k++) {
                int t = I.wlT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                if (t + 1 < stNew || t + 1 > wst2) continue;
                int id = I.wlRes[k] * I.T + t;
                if (mark[id] != stamp) { mark[id] = stamp; pos[id] = cnt; idBuf.push_back(id); dbuf.push_back((double)I.wlVal[k]); cnt++; }
                else dbuf[pos[id]] += (double)I.wlVal[k];
            }
        }
        return cnt;
    }
    void dropCells(int cnt) {
        int n = (int)idBuf.size();
        idBuf.resize(n - cnt);
        dbuf.resize(n - cnt);
    }

    double dVOf(int i, int stOld, int stNew) {
        int cnt = buildCellDelta(i, stOld, stNew);
        double d = 0.0;
        if (useW) {
            for (int k = 0; k < cnt; k++) {
                size_t id = (size_t)idBuf[k];
                double b = U[id], a = b + dbuf[k];
                d += cellW[id] * (penCell(id, a) - penCell(id, b));
            }
        } else {
            for (int k = 0; k < cnt; k++) {
                size_t id = (size_t)idBuf[k];
                double b = U[id], a = b + dbuf[k];
                d += penCell(id, a) - penCell(id, b);
            }
        }
        dropCells(cnt);
        return d;
    }

    void addR(int i, int stv, double sign) {
        int sl = I.slot(i, stv);
        {
            const int st0 = stv;
            const int st1 = stv + I.delta[sl] - 1;
            for (i64 k = I.rkStart[sl]; k < I.rkStart[sl + 1]; k++) {
                int t = I.rkRecT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                if (t + 1 < st0 || t + 1 > st1) continue;   // outside the window: checker ignores it
                double* row = &R[(size_t)t * stride];
                const double* v = &I.rkVal[I.rkRecOff[k]];
                const int S = I.S[t];
                if (sign > 0) for (int s = 0; s < S; s++) row[s] += v[s];
                else for (int s = 0; s < S; s++) row[s] -= v[s];
            }
        }
        obj1 += sign * I.cArr[sl];
    }

    void updExcCount(int i, int stOld, int stNew) {
        for (i64 a = I.adjStart[i]; a < I.adjStart[i + 1]; a++) {
            int j = I.adjJ[a];
            int sj = st[j];
            if (sj <= 0) continue;
            if (stOld > 0 && exclConflict(i, stOld, j, sj, I.adjSea[a])) nExc--;
            if (stNew > 0 && exclConflict(i, stNew, j, sj, I.adjSea[a])) nExc++;
        }
    }

    void structural(int i, int stOld, int stNew) {
        updExcCount(i, stOld, stNew);
        int cnt = buildCellDelta(i, stOld, stNew);
        for (int k = 0; k < cnt; k++) {
            size_t id = (size_t)idBuf[k];
            double b = U[id], a = b + dbuf[k];
            if (a != b) {
                V += penCell(id, a) - penCell(id, b);
                U[id] = a;
            }
        }
        dropCells(cnt);
        if (stOld > 0) addR(i, stOld, -1.0);
        if (stNew > 0) addR(i, stNew, +1.0);
    }

    // exact rank selection: if the rank is in the upper tail, keep a min-heap of the
    // top (S-rank) values; otherwise keep a max-heap of the bottom (rank+1) values.
    inline void recomputeT(int t1) {
        int t = t1 - 1;
        const int S = I.S[t];
        const double* row = &R[(size_t)t * stride];
        int rank = (int)ceil((double)S * I.quant) - 1;
        if (rank < 0) rank = 0;
        if (rank >= S) rank = S - 1;
        int kUp = S - rank;          // size of the upper group containing the answer
        double s = 0.0;
        double q;
        if (kUp * 2 <= S) {
            double* h = heap.data();
            for (int i = 0; i < kUp; i++) { h[i] = row[i]; s += row[i]; }
            for (int i = (kUp >> 1) - 1; i >= 0; i--) siftMin(h, kUp, i);
            for (int i = kUp; i < S; i++) {
                double x = row[i];
                s += x;
                if (x > h[0]) { h[0] = x; siftMin(h, kUp, 0); }
            }
            q = h[0];
        } else {
            int kLo = rank + 1;
            double* h = heap.data();
            for (int i = 0; i < kLo; i++) { h[i] = row[i]; s += row[i]; }
            for (int i = (kLo >> 1) - 1; i >= 0; i--) siftMax(h, kLo, i);
            for (int i = kLo; i < S; i++) {
                double x = row[i];
                s += x;
                if (x < h[0]) { h[0] = x; siftMax(h, kLo, 0); }
            }
            q = h[0];
        }
        double m = s / (double)S;
        extSum += max(q - m, 0.0) - max(qv[t] - mean[t], 0.0);
        mean[t] = m;
        qv[t] = q;
    }
    static inline void siftMin(double* h, int n, int i) {
        double v = h[i];
        while (true) {
            int c = 2 * i + 1;
            if (c >= n) break;
            if (c + 1 < n && h[c + 1] < h[c]) c++;
            if (h[c] >= v) break;
            h[i] = h[c]; i = c;
        }
        h[i] = v;
    }
    static inline void siftMax(double* h, int n, int i) {
        double v = h[i];
        while (true) {
            int c = 2 * i + 1;
            if (c >= n) break;
            if (c + 1 < n && h[c + 1] > h[c]) c++;
            if (h[c] <= v) break;
            h[i] = h[c]; i = c;
        }
        h[i] = v;
    }

    void recomputeAll() {
        fill(mean.begin(), mean.end(), 0.0);
        fill(qv.begin(), qv.end(), 0.0);
        fill(U.begin(), U.end(), 0.0);
        fill(R.begin(), R.end(), 0.0);
        obj1 = 0; extSum = 0; V = 0; nExc = 0;
        for (int i = 0; i < I.nInt; i++) {
            if (st[i] <= 0) continue;
            int sl = I.slot(i, st[i]);
            const int ws = st[i], we = st[i] + I.delta[sl] - 1;
            for (i64 k = I.rkStart[sl]; k < I.rkStart[sl + 1]; k++) {
                int t = I.rkRecT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                if (t + 1 < ws || t + 1 > we) continue;
                double* row = &R[(size_t)t * stride];
                const double* v = &I.rkVal[I.rkRecOff[k]];
                const int S = I.S[t];
                for (int s = 0; s < S; s++) row[s] += v[s];
            }
            for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1]; k++) {
                int t = I.wlT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                if (t + 1 < ws || t + 1 > we) continue;
                U[(size_t)I.wlRes[k] * I.T + t] += (double)I.wlVal[k];
            }
            obj1 += I.cArr[sl];
        }
        for (size_t id = 0; id < (size_t)I.nRes * I.T; id++) V += penCell(id, U[id]);
        nExc = 0;
        for (size_t k = 0; k < I.excA.size(); k++) {
            int a = I.excA[k], b = I.excB[k];
            if (st[a] > 0 && st[b] > 0 && exclConflict(a, st[a], b, st[b], I.excSea[k])) nExc++;
        }
        for (int t = 0; t < I.T; t++) {
            mean[t] = 0.0; qv[t] = 0.0;
        }
        for (int t = 1; t <= I.T; t++) recomputeT(t);
    }

    void affectedPeriods(int i, int stOld, int stNew, vector<int>& ts) {
        ts.clear();
        int dOld = (stOld > 0) ? I.deltaAt(i, stOld) : 0;
        int dNew = (stNew > 0) ? I.deltaAt(i, stNew) : 0;
        int lo1 = stOld, hi1 = stOld + dOld - 1;
        int lo2 = stNew, hi2 = stNew + dNew - 1;
        if (stOld <= 0) { lo1 = 1; hi1 = 0; }
        if (stNew <= 0) { lo2 = 1; hi2 = 0; }
        if (lo1 > hi1) { lo1 = lo2; hi1 = hi2; lo2 = 1; hi2 = 0; }
        if (lo2 <= hi2 && lo2 <= hi1 + 1 && lo1 <= hi2 + 1) { lo1 = min(lo1, lo2); hi1 = max(hi1, hi2); lo2 = 1; hi2 = 0; }
        if (lo1 < 1) lo1 = 1;
        if (hi1 > I.T) hi1 = I.T;
        if (lo2 < 1) lo2 = 1;
        if (hi2 > I.T) hi2 = I.T;
        for (int t = lo1; t <= hi1; t++) ts.push_back(t);
        if (lo2 <= hi2) for (int t = lo2; t <= hi2; t++) ts.push_back(t);
    }

    void savePeriods(const vector<int>& ts) {
        plev.push_back(PLevel{(int)psav.size(), extSum, obj1, V, nExc});
        for (int t : ts) psav.push_back(PSave{t, mean[t - 1], qv[t - 1]});
    }
    void restorePeriods() {
        PLevel lv = plev.back();
        plev.pop_back();
        for (int k = lv.off; k < (int)psav.size(); k++) {
            mean[psav[k].t - 1] = psav[k].m;
            qv[psav[k].t - 1] = psav[k].q;
        }
        psav.resize(lv.off);
        extSum = lv.ext; obj1 = lv.obj1; V = lv.V; nExc = lv.nexc;
    }
    void recomputePeriods(const vector<int>& ts) { for (int t : ts) recomputeT(t); }

    // apply without saving the period snapshot (caller reverts via a full rebuild)
    double applyMoveFast(int i, int stNew, vector<int>& ts) {
        int stOld = st[i];
        if (stOld == stNew) return 0.0;
        double g0 = G();
        affectedPeriods(i, stOld, stNew, ts);
        structural(i, stOld, stNew);
        st[i] = stNew;
        recomputePeriods(ts);
        return G() - g0;
    }

    double applyMove(int i, int stNew, vector<int>& ts) {
        int stOld = st[i];
        double g0 = G();
        affectedPeriods(i, stOld, stNew, ts);
        savePeriods(ts);
        structural(i, stOld, stNew);
        st[i] = stNew;
        recomputePeriods(ts);
        return G() - g0;
    }
    void undoMove(int i, int stOld) {
        int stNew = st[i];
        structural(i, stNew, stOld);
        st[i] = stOld;
        restorePeriods();
    }

    inline bool exclConflict(int i, int sti, int j, int stj, int sea) const {
        int di = I.deltaAt(i, sti), dj = I.deltaAt(j, stj);
        int lo = max(sti, stj), hi = min(sti + di, stj + dj);
        if (lo >= hi) return false;
        const int* pf = &I.seaPref[(size_t)sea * (I.T + 2)];
        return (pf[hi - 1] - pf[lo - 1]) > 0;
    }
    bool pairOk(int i, int sti, int j, int stj) {
        for (i64 a = I.adjStart[i]; a < I.adjStart[i + 1]; a++)
            if (I.adjJ[a] == j && exclConflict(i, sti, j, stj, I.adjSea[a])) return false;
        return true;
    }
    bool exclOk(int i, int stNew) {
        for (i64 a = I.adjStart[i]; a < I.adjStart[i + 1]; a++) {
            int j = I.adjJ[a];
            int sj = st[j];
            if (sj <= 0) continue;
            if (exclConflict(i, stNew, j, sj, I.adjSea[a])) return false;
        }
        return true;
    }

    vector<int> makeOrder(int mode, double noiseScale) {
        vector<int> ord(I.nInt);
        iota(ord.begin(), ord.end(), 0);
        if (mode == 0) {
            for (int i = I.nInt - 1; i > 0; i--) swap(ord[i], ord[rng.i01(i + 1)]);
        } else if (mode == 1) {
            vector<pair<double, int>> key(I.nInt);
            for (int i = 0; i < I.nInt; i++) {
                i64 sl = I.slotBase[i];
                double wl = (double)(I.wlStart[sl + 1] - I.wlStart[sl]);
                int d = (sl < (int)I.delta.size() && I.delta[sl] > 0) ? I.delta[sl] : 1;
                double fp = wl / (double)d * (double)d;
                key[i] = {-fp, i};
            }
            sort(key.begin(), key.end());
            for (int i = 0; i < I.nInt; i++) ord[i] = key[i].second;
        } else if (mode == 2) {
            vector<pair<double, int>> key(I.nInt);
            for (int i = 0; i < I.nInt; i++) key[i] = {(double)I.slotHi[i] + noiseScale * rng.u01() * 6.0, i};
            sort(key.begin(), key.end());
            for (int i = 0; i < I.nInt; i++) ord[i] = key[i].second;
        } else {
            vector<pair<double, int>> key(I.nInt);
            for (int i = 0; i < I.nInt; i++) key[i] = {rng.u01(), i};
            sort(key.begin(), key.end());
            for (int i = 0; i < I.nInt; i++) ord[i] = key[i].second;
        }
        return ord;
    }

    void placeRaw(int i, int bs) {
        updExcCount(i, 0, bs);
        st[i] = bs;
        int cnt = buildCellDelta(i, 0, bs);
        for (int k = 0; k < cnt; k++) {
            size_t id = (size_t)idBuf[k];
            double b = U[id], a = b + dbuf[k];
            V += penCell(id, a) - penCell(id, b);
            U[id] = a;
        }
        dropCells(cnt);
        addR(i, bs, +1.0);
    }

    void construct(const vector<int>& ord, double noiseScale, double lamC) {
        fill(st.begin(), st.end(), 0);
        fill(U.begin(), U.end(), 0.0);
        V = 0; nExc = 0;
        for (int oi = 0; oi < (int)ord.size(); oi++) {
            int i = ord[oi];
            int hi = I.slotHi[i];
            if (hi <= 0) { st[i] = 1; continue; }
            int bs = -1;
            double best = 1e300;
            for (int stt : cand[i]) {
                if (!exclOk(i, stt)) continue;
                double sc = I.cArr[I.slot(i, stt)] * (1.0 + noiseScale * rng.u01()) + lamC * dVOf(i, 0, stt);
                if (sc < best) { best = sc; bs = stt; }
            }
            if (bs < 0) {
                for (int stt = 1; stt <= hi; stt++) {
                    if (!exclOk(i, stt)) continue;
                    double sc = I.cArr[I.slot(i, stt)] + lamC * dVOf(i, 0, stt);
                    if (sc < best) { best = sc; bs = stt; }
                }
            }
            if (bs < 0) bs = cand[i].empty() ? 1 : cand[i][0];
            placeRaw(i, bs);
        }
        recomputeAll();
    }

    void recomputeVPenalty() {
        V = 0;
        for (size_t id = 0; id < (size_t)I.nRes * I.T; id++) V += penCell(id, U[id]);
    }

    long long countViolCells() const {
        long long v = 0;
        for (size_t id = 0; id < (size_t)I.nRes * I.T; id++)
            if (U[id] > I.rmax[id] + 1e-6 || U[id] < I.rmin[id] - 1e-6) v++;
        return v;
    }
    long long countExcViol() {
        long long v = 0;
        for (size_t k = 0; k < I.excA.size(); k++) {
            int a = I.excA[k], b = I.excB[k];
            if (st[a] > 0 && st[b] > 0 && exclConflict(a, st[a], b, st[b], I.excSea[k])) v++;
        }
        return v;
    }

    void noteState() {
        if (isFeas()) {
            double f = F();
            if (f < bestFeasF) {
                bestFeasF = f;
                bestFeas = st;
                if (trace.size() < 200000 && f < bestTraced - 1e-12) {
                    bestTraced = f;
                    trace.push_back({nowSec() - tZero, f});
                }
            }
        }
        if (V < bestAnyV) { bestAnyV = V; bestAny = st; bestAnyF = F(); }
    }

    int pickCand(int i) {
        const auto& c = cand[i];
        if (c.empty()) return -1;
        return c[rng.i01((int)c.size())];
    }

    void oneMove(double temp) {
        int i = rng.i01(I.nInt);
        if (I.slotHi[i] <= 0) return;
        int stOld = st[i];
        int stNew;
        double u = rng.u01();
        if (u < 0.60) stNew = pickCand(i);
        else if (u < 0.85) { int d = 1 + rng.i01(3); stNew = stOld + ((rng.u01() < 0.5) ? -d : d); }
        else stNew = 1 + rng.i01(I.slotHi[i]);
        if (stNew == stOld || stNew < 1 || stNew > I.slotHi[i] || !I.okStart[I.slot(i, stNew)]) return;
        if (!exclOk(i, stNew)) return;
        iters++;
        double d = applyMove(i, stNew, tsBuf);
        if (d <= 0.0 || rng.u01() < exp(-d / temp)) {
            accepted++;
            noteState();
        } else {
            undoMove(i, stOld);
        }
    }

    void swapMove(double temp) {
        int i = rng.i01(I.nInt), j = rng.i01(I.nInt);
        if (i == j) return;
        int si = st[i], sj = st[j];
        if (si <= 0 || sj <= 0) return;
        if (sj > I.slotHi[i] || si > I.slotHi[j]) return;
        if (!I.okStart[I.slot(i, sj)] || !I.okStart[I.slot(j, si)]) return;
        {
            int a0 = st[i], b0 = st[j];
            st[i] = 0; st[j] = 0;
            bool ok = exclOk(i, sj) && exclOk(j, si) && pairOk(i, sj, j, si);
            st[i] = a0; st[j] = b0;
            if (!ok) return;
        }
        iters++;
        double g0 = G();
        vector<int> ts1, ts2;
        applyMove(i, sj, ts1);
        applyMove(j, si, ts2);
        double d = G() - g0;
        if (d <= 0.0 || rng.u01() < exp(-d / temp)) {
            accepted++;
            noteState();
        } else {
            undoMove(j, sj);
            undoMove(i, si);
        }
    }

    void ruinRecreate(double temp) {
        int k = P.ruinMin + rng.i01(max(1, P.ruinMax - P.ruinMin + 1));
        if (k > I.nInt) k = I.nInt;
        vector<int> chosen;
        chosen.reserve(k);
        chosen.push_back(rng.i01(I.nInt));
        while ((int)chosen.size() < k) {
            int r = rng.i01(I.nInt);
            bool dup = false;
            for (int c : chosen) if (c == r) { dup = true; break; }
            if (!dup) chosen.push_back(r);
        }
        vector<int> saved(k);
        for (int a = 0; a < k; a++) saved[a] = st[chosen[a]];
        double g0 = G();
        for (int a = 0; a < k; a++) {
            int c = chosen[a];
            int so = st[c];
            if (so <= 0) continue;
            structural(c, so, 0);
            st[c] = 0;
        }
        bool okAll = true;
        for (int a = 0; a < k; a++) {
            int c = chosen[a];
            if (st[c] > 0) continue;
            int bs = -1;
            double best = 1e300;
            for (int stt : cand[c]) {
                if (!exclOk(c, stt)) continue;
                double sc = I.cArr[I.slot(c, stt)] * (1.0 + 0.05 * rng.u01()) + lam * dVOf(c, 0, stt);
                if (sc < best) { best = sc; bs = stt; }
            }
            if (bs < 0) { okAll = false; break; }
            placeRaw(c, bs);
        }
        vector<int> ts;
        ts.reserve(256);
        for (int a = 0; a < k; a++) {
            int c = chosen[a];
            if (saved[a] > 0) {
                int d = I.deltaAt(c, saved[a]);
                for (int t = saved[a]; t <= saved[a] + d - 1 && t <= I.T; t++) ts.push_back(t);
            }
            if (st[c] > 0) {
                int d = I.deltaAt(c, st[c]);
                for (int t = st[c]; t <= st[c] + d - 1 && t <= I.T; t++) ts.push_back(t);
            }
        }
        sort(ts.begin(), ts.end());
        ts.erase(unique(ts.begin(), ts.end()), ts.end());
        for (int t : ts) recomputeT(t);
        if (!okAll) {
            for (int a = 0; a < k; a++) {
                int c = chosen[a];
                if (st[c] > 0) { structural(c, st[c], 0); st[c] = 0; }
            }
            for (int a = 0; a < k; a++) {
                int c = chosen[a];
                if (saved[a] > 0) { structural(c, 0, saved[a]); st[c] = saved[a]; }
            }
            for (int t : ts) recomputeT(t);
            return;
        }
        double d = G() - g0;
        if (d <= 0.0 || rng.u01() < exp(-d / temp)) {
            accepted++;
            noteState();
        } else {
            for (int a = 0; a < k; a++) {
                int c = chosen[a];
                if (st[c] > 0) { structural(c, st[c], 0); st[c] = 0; }
            }
            for (int a = 0; a < k; a++) {
                int c = chosen[a];
                if (saved[a] > 0) { structural(c, 0, saved[a]); st[c] = saved[a]; }
            }
            for (int t : ts) recomputeT(t);
        }
        applied += k;
    }



    // quantile-targeted move: focus on a period with a large excess and try to
    // improve one of the interventions that is ongoing there.
    void targetedMove(double temp) {
        int bestT = -1;
        double bestQ = -1e300;
        for (int k = 0; k < 4; k++) {
            int t = rng.i01(I.T);
            double e = qv[t] - mean[t];
            if (e > bestQ) { bestQ = e; bestT = t + 1; }
        }
        if (bestT < 1) return;
        int chosen = -1;
        for (int k = 0; k < 24; k++) {
            int i = rng.i01(I.nInt);
            if (st[i] <= 0) continue;
            int s = st[i];
            int d = I.deltaAt(i, s);
            if (s <= bestT && bestT < s + d) { chosen = i; break; }
        }
        if (chosen < 0) return;
        int cur = st[chosen];
        double bestD = -1e-12;
        int bs = cur;
        int nc = (int)cand[chosen].size();
        int lim = nc;
        int step = 1;
        if (nc > P.targetCand) { lim = P.targetCand; step = max(1, nc / P.targetCand); }
        iters++;
        for (int k = 0; k < lim; k++) {
            int stt = (nc > P.targetCand) ? cand[chosen][(int)rng.i01(nc)] : cand[chosen][k];
            if (stt == cur) continue;
            if (!exclOk(chosen, stt)) continue;
            double d = applyMove(chosen, stt, tsBuf);
            if (d < bestD) { bestD = d; bs = stt; }
            undoMove(chosen, cur);
        }
        (void)step;
        if (bs != cur) {
            applyMove(chosen, bs, tsBuf);
            accepted++;
            noteState();
        }
    }

    // ---------------------------------------------------------------- LNS phase
    // destroy k interventions, reinsert them one by one evaluating the exact
    // objective delta over the most promising candidate starts.
    void lns(double tStop, double temp, int kMin, int kMax, int nCand) {
        vector<int> chosen, saved, order;
        vector<pair<double, int>> rank;
        while (nowSec() < tStop) {
            int k = kMin + rng.i01(max(1, kMax - kMin + 1));
            if (k > I.nInt) k = I.nInt;
            chosen.clear();
            chosen.push_back(rng.i01(I.nInt));
            while ((int)chosen.size() < k) {
                int r = rng.i01(I.nInt);
                bool dup = false;
                for (int c : chosen) if (c == r) { dup = true; break; }
                if (!dup) chosen.push_back(r);
            }
            saved.assign(k, 0);
            for (int a = 0; a < k; a++) saved[a] = st[chosen[a]];
            double g0 = G();
            vector<int> stBackup = st;
            for (int a = 0; a < k; a++) if (st[chosen[a]] > 0) applyMoveFast(chosen[a], 0, tsBuf);
            order = chosen;
            for (int a = (int)order.size() - 1; a > 0; a--) swap(order[a], order[rng.i01(a + 1)]);
            bool okAll = true;
            for (int c : order) {
                if (I.slotHi[c] <= 0) continue;
                rank.clear();
                for (int stt : cand[c]) {
                    if (!exclOk(c, stt)) continue;
                    double sc = I.cArr[I.slot(c, stt)] + lam * dVOf(c, 0, stt);
                    rank.push_back({sc, stt});
                }
                if (rank.empty()) { okAll = false; break; }
                int take = min((int)rank.size(), nCand);
                if (take < (int)rank.size()) {
                    nth_element(rank.begin(), rank.begin() + take, rank.end());
                    rank.resize(take);
                }
                int bs = -1;
                double bestD = 1e300;
                for (auto& pr : rank) {
                    int stt = pr.second;
                    double d = applyMoveFast(c, stt, tsBuf);
                    if (d < bestD) { bestD = d; bs = stt; }
                    applyMoveFast(c, 0, tsBuf);
                }
                if (bs < 0) { okAll = false; break; }
                applyMoveFast(c, bs, tsBuf);
            }
            if (!okAll) {
                st = stBackup;
                recomputeAll();
                continue;
            }
            double d = G() - g0;
            iters += (long long)k * nCand;
            if (d <= 0.0 || rng.u01() < exp(-d / temp)) {
                accepted++;
                noteState();
            } else {
                st = stBackup;
                recomputeAll();
            }
        }
    }


    // number of partners that would conflict with intervention i at start stt
    int confCount(int i, int stt) {
        int c = 0;
        for (i64 a = I.adjStart[i]; a < I.adjStart[i + 1]; a++) {
            int j = I.adjJ[a];
            int sj = st[j];
            if (sj <= 0) continue;
            if (j == i) continue;
            if (exclConflict(i, stt, j, sj, I.adjSea[a])) c++;
        }
        return c;
    }

    // cheap move: updates U/V/nExc/csum only (the caller must rebuild the
    // period tables afterwards when it needs the objective again)
    void fastMove(int i, int stNew) {
        structural(i, st[i], stNew);
        st[i] = stNew;
    }

    // min-conflicts walk on the exclusion graph: repairs single remaining conflicts
    // that a pure best-improvement pass cannot remove (C_13 case).
    bool repairExc(double tStop) {
        if (nExc == 0) return true;
        vector<size_t> conf;
        for (int step = 0; step < 60000; step++) {
            if ((step & 31) == 0 && nowSec() > tStop) goto done;
            conf.clear();
            for (size_t k = 0; k < I.excA.size(); k++) {
                int a = I.excA[k], b = I.excB[k];
                if (st[a] <= 0 || st[b] <= 0) continue;
                if (exclConflict(a, st[a], b, st[b], I.excSea[k])) conf.push_back(k);
            }
            if (conf.empty()) goto done;
            {
                size_t k = conf[rng.i01((int)conf.size())];
                int a = I.excA[k], b = I.excB[k];
                int i = (rng.u01() < 0.5) ? a : b;
                int hi = I.slotHi[i];
                if (hi <= 0) continue;
                int cur = st[i];
                int bestC = confCount(i, cur);
                int bs = cur;
                for (int t = 0; t < 220; t++) {
                    int stt = (hi <= 512) ? (1 + rng.i01(hi)) : cand[i][rng.i01((int)cand[i].size())];
                    if (stt == cur || !I.okStart[I.slot(i, stt)]) continue;
                    int c = confCount(i, stt);
                    if (c < bestC || (c == bestC && rng.u01() < 0.35)) { bestC = c; bs = stt; }
                    if (bestC == 0) break;
                }
                if (bs == cur || (rng.u01() < 0.25)) {
                    // WalkSAT-style random walk: a random valid start, conflicts allowed.
                    // Needed when a single-variable change cannot remove the conflict.
                    int stt = (hi <= 512) ? (1 + rng.i01(hi)) : cand[i][rng.i01((int)cand[i].size())];
                    if (stt == cur || !I.okStart[I.slot(i, stt)]) continue;
                    bs = stt;
                }
                fastMove(i, bs);
            }
        }
    done:
        if (nExc == 0) return true;
        // fall back to the strict best-improvement pass
        for (int pass = 0; pass < 50; pass++) {
            if (nowSec() > tStop) return nExc == 0;
            bool improved = false;
            for (size_t k = 0; k < I.excA.size(); k++) {
                int a = I.excA[k], b = I.excB[k];
                if (st[a] <= 0 || st[b] <= 0) continue;
                if (!exclConflict(a, st[a], b, st[b], I.excSea[k])) continue;
                for (int w = 0; w < 2; w++) {
                    int i = (w == 0) ? a : b;
                    int hi = I.slotHi[i];
                    int cur = st[i];
                    int curC = confCount(i, cur);
                    if (curC == 0) continue;
                    int bs = cur, bestC = curC;
                    double bestCost = I.cArr[I.slot(i, cur)];
                    for (int stt = 1; stt <= hi; stt++) {
                        if (stt == cur || !I.okStart[I.slot(i, stt)]) continue;
                        int c = confCount(i, stt);
                        if (c > bestC) continue;
                        double cost = I.cArr[I.slot(i, stt)];
                        if (c < bestC || cost < bestCost) { bestC = c; bestCost = cost; bs = stt; }
                    }
                    if (bs != cur) { fastMove(i, bs); improved = true; if (nExc == 0) return true; }
                }
            }
            if (!improved) return nExc == 0;
        }
        return nExc == 0;
    }

    // targeted min-conflicts repair: only interventions touching a violated cell are moved
    bool repair(double tStop) {
        cellW.assign((size_t)I.nRes * I.T, 1.0);
        useW = true;
        bool ok = repairImpl(tStop);
        useW = false;
        return ok;
    }

    bool repairImpl(double tStop) {
        if (nExc > 0) {
            if (repairExc(tStop)) recomputeAll();
        }
        vector<int> ord(I.nInt);
        iota(ord.begin(), ord.end(), 0);
        vector<char> badCell((size_t)I.nRes * I.T, 0);
        vector<int> dirty, badList;
        int stall = 0;
        for (int pass = 0; pass < 100000; pass++) {
            if (nowSec() > tStop) return isFeas();
            repairPasses++;
            // violated cells
            bool anyBad = false;
            fill(badCell.begin(), badCell.end(), 0);
            for (size_t id = 0; id < (size_t)I.nRes * I.T; id++) {
                double u = U[id];
                if (u > I.rmax[id] + 1e-6 || u < I.rmin[id] - 1e-6) { badCell[id] = 1; anyBad = true; }
            }
            if (!anyBad && nExc == 0) return true;
            badList.clear();
            for (size_t id = 0; id < (size_t)I.nRes * I.T; id++)
                if (badCell[id]) badList.push_back((int)id);
            // interventions whose current window touches a bad cell
            dirty.clear();
            for (int i = 0; i < I.nInt; i++) {
                if (st[i] <= 0) { dirty.push_back(i); continue; }
                int sl = I.slot(i, st[i]);
                bool d = false;
                for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1] && !d; k++) {
                    int t = I.wlT[k] - 1;
                    if (t < 0 || t >= I.T) continue;
                    if (badCell[(size_t)I.wlRes[k] * I.T + t]) d = true;
                }
                if (d) dirty.push_back(i);
            }
            if (dirty.empty()) {
                for (int i = 0; i < I.nInt; i++) dirty.push_back(i);
            }
            for (int a = (int)dirty.size() - 1; a > 0; a--) swap(dirty[a], dirty[rng.i01(a + 1)]);
            long long improvements = 0;
            for (int i : dirty) {
                if (I.slotHi[i] <= 0) continue;
                int cur = st[i];
                int bs = cur;
                double bestD = -1e-12;
                int hi = I.slotHi[i];
                int nAll = (hi <= 512) ? hi : 0;
                for (int stt = 1; stt <= nAll; stt++) {
                    if (stt == cur) continue;
                    if (!I.okStart[I.slot(i, stt)]) continue;
                    if (!exclOk(i, stt)) continue;
                    double d = dVOf(i, cur, stt);
                    if (d < bestD - 1e-15) { bestD = d; bs = stt; }
                }
                if (nAll == 0) {
                    for (int stt : cand[i]) {
                        if (stt == cur) continue;
                        if (!exclOk(i, stt)) continue;
                        double d = dVOf(i, cur, stt);
                        if (d < bestD - 1e-15) { bestD = d; bs = stt; }
                    }
                }
                if (bs != cur) { applyMove(i, bs, tsBuf); improvements++; }
            }
            if (isFeas()) return true;
            if (improvements == 0) {
                // breakout: raise the weight of the persistently violated cells
                stall++;
                for (int id : badList) cellW[id] += 1.0;
                if (stall > 4000) return isFeas();
                for (int tries = 0; tries < 4 && !dirty.empty(); tries++) {
                    int i = dirty[rng.i01((int)dirty.size())];
                    int hi = I.slotHi[i];
                    if (hi <= 0) continue;
                    int stt = 1 + rng.i01(hi);
                    if (!I.okStart[I.slot(i, stt)] || !exclOk(i, stt)) continue;
                    if (stt != st[i]) applyMove(i, stt, tsBuf);
                }
            } else stall = 0;
        }
        return isFeas();
    }

    void polish(double tStop, bool keepFeasible) {
        vector<int> ord(I.nInt);
        iota(ord.begin(), ord.end(), 0);
        for (int a = I.nInt - 1; a > 0; a--) swap(ord[a], ord[rng.i01(a + 1)]);
        for (int idx = 0; idx < I.nInt; idx++) {
            if ((idx & 7) == 0 && nowSec() > tStop) return;
            int i = ord[idx];
            int hi = I.slotHi[i];
            if (hi <= 0) continue;
            int cur = st[i];
            double bestD = -1e-12;
            int bs = cur;
            for (int stt : cand[i]) {
                if (stt == cur) continue;
                if (!exclOk(i, stt)) continue;
                if (keepFeasible && dVOf(i, cur, stt) > 0) continue;
                double d = applyMove(i, stt, tsBuf);
                if (d < bestD) { bestD = d; bs = stt; }
                undoMove(i, cur);
            }
            if (bs != cur) {
                applyMove(i, bs, tsBuf);
                accepted++;
                noteState();
            }
        }
    }
};

// ------------------------------------------------------------------ stats mode
static void printStats(Instance& I) {
    printf("T=%d nRes=%d nInt=%d nExc=%d nSea=%d maxS=%d sumS=%lld quant=%.4f alpha=%.4f\n",
           I.T, I.nRes, I.nInt, (int)I.excA.size(), I.nSea, I.maxS, I.sumS, I.quant, I.alpha);
    printf("slots=%zu wlRec=%lld rkRec=%lld rkVal=%lld rkMemMB=%.1f\n",
           (size_t)I.delta.size(), (long long)I.totalWlRec, (long long)I.totalRkRec, (long long)I.totalRkVal,
           (double)I.totalRkVal * 4.0 / 1048576.0);
    long long totStart = 0;
    int minHi = 1 << 30, maxHi = 0;
    long long totD = 0;
    for (int i = 0; i < I.nInt; i++) {
        totStart += I.slotHi[i];
        minHi = min(minHi, I.slotHi[i]);
        maxHi = max(maxHi, I.slotHi[i]);
        for (int st = 1; st <= I.slotHi[i]; st++) totD += I.deltaAt(i, st);
    }
    printf("feasibleStarts=%lld avgPerInt=%.2f minHi=%d maxHi=%d avgDelta=%.2f\n",
           totStart, (double)totStart / max(1, I.nInt), minHi, maxHi, (double)totD / max(1LL, totStart));
    // utilisation: expected load / mean capacity, per resource
    double worst = 0; int worstR = -1;
    for (int r = 0; r < I.nRes; r++) {
        double acc = 0.0, cap = 0.0;
        for (int i = 0; i < I.nInt; i++) {
            if (I.slotHi[i] <= 0) continue;
            double s = 0.0;
            for (int st = 1; st <= I.slotHi[i]; st++) {
                int sl = I.slot(i, st);
                for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1]; k++)
                    if (I.wlRes[k] == r) s += I.wlVal[k];
            }
            acc += s / (double)I.slotHi[i];
        }
        for (int t = 0; t < I.T; t++) cap += I.rmax[(size_t)r * I.T + t];
        double ratio = (acc / (double)I.T) / max(1e-9, cap / (double)I.T);
        if (ratio > worst) { worst = ratio; worstR = r; }
    }
    double minSum = 0;
    for (size_t k = 0; k < I.rmin.size(); k++) minSum += I.rmin[k];
    printf("maxUtilisation=%.4f (res %d) sumOfMinBounds=%.3f\n", worst, worstR, minSum);
    double cmin = 1e300, cmax = -1e300, csum = 0;
    long long cnt = 0;
    for (int i = 0; i < I.nInt; i++) for (int st = 1; st <= I.slotHi[i]; st++) {
        double c = I.cArr[I.slot(i, st)];
        cmin = min(cmin, c); cmax = max(cmax, c); csum += c; cnt++;
    }
    printf("unaryCost min=%.6g max=%.6g avg=%.6g\n", cmin, cmax, csum / max(1LL, cnt));
}

// ------------------------------------------------------------------ main
static void writeSolution(const char* path, const Instance& I, const vector<int>& sol) {
    string buf;
    buf.reserve((size_t)I.nInt * 24 + 64);
    char tmp[32];
    for (int i = 0; i < I.nInt; i++) {
        buf.append(I.intName[i]);
        buf.push_back(' ');
        int n = snprintf(tmp, sizeof(tmp), "%d", sol[i] > 0 ? sol[i] : 1);
        buf.append(tmp, (size_t)n);
        buf.push_back('\n');
    }
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
}

static VerifyRes verifySolution(Instance& I, Solver& S, const vector<int>& sol) {
    VerifyRes v;
    int T = I.T;
    vector<double> UU((size_t)I.nRes * T, 0.0);
    v.schedViol = 0;
    for (int i = 0; i < I.nInt; i++) {
        int s = sol[i];
        if (s < 1 || s > T || s > I.tmax[i]) { v.schedViol++; continue; }
        int d = I.deltaAt(i, s);
        if (s + d - 1 > T) { v.schedViol++; continue; }
        int sl = I.slot(i, s);
        const int ws = s, we = s + I.delta[sl] - 1;
        for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1]; k++) {
            int t = I.wlT[k] - 1;
            if (t < 0 || t >= T) continue;
            if (t + 1 < ws || t + 1 > we) continue;
            UU[(size_t)I.wlRes[k] * T + t] += (double)I.wlVal[k];
        }
    }
    for (int r = 0; r < I.nRes; r++)
        for (int t = 0; t < T; t++) {
            size_t id = (size_t)r * T + t;
            double u = UU[id];
            if (u > I.rmax[id] + 1e-5 || u < I.rmin[id] - 1e-5) v.resViol++;
        }
    v.excViol = 0;
    for (size_t k = 0; k < I.excA.size(); k++) {
        int a = I.excA[k], b = I.excB[k];
        int sa = sol[a], sb = sol[b];
        if (sa < 1 || sb < 1) { v.excViol++; continue; }
        if (S.exclConflict(a, sa, b, sb, I.excSea[k])) v.excViol++;
    }
    v.viol = v.schedViol + v.resViol + v.excViol;
    int stride = I.maxS;
    vector<double> RR((size_t)T * stride, 0.0);
    for (int i = 0; i < I.nInt; i++) {
        int s = sol[i];
        if (s < 1 || s > I.tmax[i]) continue;
        int sl = I.slot(i, s);
        const int ws2 = s, we2 = s + I.delta[sl] - 1;
        for (i64 k = I.rkStart[sl]; k < I.rkStart[sl + 1]; k++) {
            int t = I.rkRecT[k] - 1;
            if (t < 0 || t >= T) continue;
            if (t + 1 < ws2 || t + 1 > we2) continue;
            double* row = &RR[(size_t)t * stride];
            const double* vv = &I.rkVal[I.rkRecOff[k]];
            const int Sn = I.S[t];
            for (int q = 0; q < Sn; q++) row[q] += vv[q];
        }
    }
    vector<double> tmpI((size_t)stride);
    double o1 = 0, o2 = 0;
    for (int t = 0; t < T; t++) {
        const int Sn = I.S[t];
        double* row = &RR[(size_t)t * stride];
        double s = 0.0;
        for (int q = 0; q < Sn; q++) s += row[q];
        double m = s / (double)Sn;
        int rank = (int)ceil((double)Sn * I.quant) - 1;
        if (rank < 0) rank = 0;
        if (rank >= Sn) rank = Sn - 1;
        memcpy(tmpI.data(), row, sizeof(double) * (size_t)Sn);
        nth_element(tmpI.data(), tmpI.data() + rank, tmpI.data() + Sn);
        double q = tmpI[rank];
        o1 += m;
        o2 += max(q - m, 0.0);
    }
    v.obj1 = o1 / T;
    v.obj2 = o2 / T;
    v.obj = I.alpha * v.obj1 + (1.0 - I.alpha) * v.obj2;
    v.feasible = (v.viol == 0);
    return v;
}

int main(int argc, char** argv) {
    double tProcStart = nowSec();
    initPow();
    Params P;
    const char* instPath = nullptr;
    const char* outSol = nullptr;
    const char* outStats = nullptr;
    bool statsOnly = false, verbose = false;
    for (int k = 1; k < argc; k++) {
        string a = argv[k];
        auto nextv = [&]() -> const char* { return (k + 1 < argc) ? argv[++k] : nullptr; };
        if (a == "--instance") instPath = nextv();
        else if (a == "--out") outSol = nextv();
        else if (a == "--stats-file") outStats = nextv();
        else if (a == "--stats") statsOnly = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--seed") P.seed = (uint64_t)atoll(nextv());
        else if (a == "--time") P.timeLimit = atof(nextv());
        else if (a == "--reserve") P.reserve = atof(nextv());
        else if (a == "--candcap") P.candCap = atoi(nextv());
        else if (a == "--ordermode") P.orderMode = atoi(nextv());
        else if (a == "--noise") P.noise = atof(nextv());
        else if (a == "--restarts") P.restarts = atoi(nextv());
        else if (a == "--psingle") P.pSingle = atof(nextv());
        else if (a == "--pswap") P.pSwap = atof(nextv());
        else if (a == "--pruin") P.pRuin = atof(nextv());
        else if (a == "--ruinmin") P.ruinMin = atoi(nextv());
        else if (a == "--ruinmax") P.ruinMax = atoi(nextv());
        else if (a == "--t0") P.t0frac = atof(nextv());
        else if (a == "--t1") P.t1frac = atof(nextv());
        else if (a == "--polish") P.polishFrac = atof(nextv());
        else if (a == "--lamc") P.lamC = atof(nextv());
        else if (a == "--lamscale") P.lamScale = atof(nextv());
        else if (a == "--lnsfrac") P.lnsFrac = atof(nextv());
        else if (a == "--sacycles") P.saCycles = atoi(nextv());
        else if (a == "--ptarget") P.pTarget = atof(nextv());
        else if (a == "--targetcand") P.targetCand = atoi(nextv());
        else if (a == "--restartgap") P.restartGap = atof(nextv());
        else if (a == "--lnskmin") P.lnsKMin = atoi(nextv());
        else if (a == "--lnskmax") P.lnsKMax = atoi(nextv());
        else if (a == "--lnscand") P.lnsCand = atoi(nextv());
        else if (a == "--laminit") P.lamInit = atof(nextv());
        else { fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (!instPath) { fprintf(stderr, "need --instance\n"); return 2; }
    Instance I;
    if (!I.parse(instPath, verbose)) return 2;
    I.finalize(verbose);
    double tParsed = nowSec();
    if (statsOnly) { printStats(I); return 0; }

    double lamScale = P.lamScale;
    double lamLo = 1e-6, lamHi = 1e12;
    double budgetEnd = tProcStart + P.timeLimit - P.reserve;
    if (budgetEnd < tParsed + 1.0) budgetEnd = tParsed + 1.0;
    Solver S(I, P);
    S.tZero = tProcStart;

    // ---- phase 1: construction + repair until feasible
    double tRepairStop = min(tParsed + (budgetEnd - tParsed) * P.repairFrac, budgetEnd - 0.5);
    double lamC = P.lamC;
    bool feasible = false;
    for (int attempt = 0; attempt < P.restarts; attempt++) {
        int mode = (attempt == 0) ? P.orderMode : (P.orderMode + attempt) % 4;
        vector<int> ord = S.makeOrder(mode, P.noise);
        S.construct(ord, P.noise, lamC);
        long long vc = S.countViolCells();
        if (verbose) fprintf(stderr, "[construct] att=%d mode=%d violCells=%lld F=%.6f\n", attempt, mode, vc, S.F());
        if (S.V <= 0.0) { feasible = true; S.noteState(); break; }
        bool ok = S.repair(min(tRepairStop, nowSec() + 5.0));
        if (verbose) fprintf(stderr, "[repair] att=%d violCells=%lld V=%.6g F=%.6f\n", attempt, S.countViolCells(), S.V, S.F());
        if (ok) { feasible = true; S.noteState(); break; }
        S.noteState();
        if (nowSec() > tRepairStop) break;
        lamC *= 3.0;
    }
    if (!feasible && !S.isFeas()) {
        // dedicated repair with growing lambda influence is not needed: V-only repair
        feasible = S.repair(tRepairStop);
        if (verbose) fprintf(stderr, "[repair2] feasible=%d violCells=%lld V=%.6g\n", (int)feasible, S.countViolCells(), S.V);
    }
    if (feasible) {
        S.noteState();
        if (verbose) fprintf(stderr, "[phase1] feasible F=%.6f\n", S.F());
    } else if (verbose) {
        fprintf(stderr, "[phase1] INFEASIBLE after repair: V=%.6g cells=%lld\n", S.V, S.countViolCells());
    }

    // penalty weight: convert F-scale to V-scale
    {
        S.recomputeVPenalty();
        double fnow = max(1e-9, S.F());
        S.lam = max(1.0, lamScale * fnow);
        S.lamE = 1e6 * fnow;
        lamLo = S.lam * 1e-2;
        lamHi = S.lam * 1e2;
    }
    if (S.nExc > 0) { S.repairExc(budgetEnd); S.recomputeAll(); }
    if (verbose) fprintf(stderr, "[lam] init=%.6g lamE=%.6g V=%.6g nExc=%d F=%.6f\n",
                         S.lam, S.lamE, S.V, S.nExc, S.F());

    // ---- phase 2: simulated annealing on G = F + lambda V
    double tSaEnd = tParsed + max(1.0, (budgetEnd - tParsed) * (1.0 - P.polishFrac - P.lnsFrac));
    double F0 = max(1e-9, S.F());
    double Tstart = P.t0frac * F0, Tend = max(1e-12, P.t1frac * F0);
    long long lastAdapt = 0;
    long long vZeroIters = 0;
    int nCycles = max(1, P.saCycles);
    double cycleLen = max(0.05, (tSaEnd - tParsed) / (double)nCycles);
    double cycleEnd = tParsed + cycleLen;
    int cycleNo = 0;
    while (nowSec() < tSaEnd) {
        if (nowSec() >= cycleEnd) {
            cycleNo++;
            cycleEnd += cycleLen;
            if (!S.bestFeas.empty() && S.F() > S.bestFeasF * (1.0 + P.restartGap)) {
                S.st = S.bestFeas;
                S.recomputeAll();
            }
        }
        double frac = (nowSec() - tParsed) / max(1e-9, (tSaEnd - tParsed));
        double cfrac = (frac * nCycles) - floor(frac * nCycles);
        if (frac > 1) { frac = 1; cfrac = 1; }
        double temp = Tstart * pow(Tend / Tstart, cfrac);
        for (int rep = 0; rep < 32; rep++) {
            double u = S.rng.u01();
            if (u < P.pTarget) S.targetedMove(temp);
            else if (u < P.pTarget + P.pSingle) S.oneMove(temp);
            else if (u < P.pTarget + P.pSingle + P.pSwap) S.swapMove(temp);
            else S.ruinRecreate(temp);
        }
        if (S.iters - lastAdapt > 2000) {
            lastAdapt = S.iters;
            if (S.nExc > 0) {
                // exclusion conflicts block every feasible move; repair them explicitly
                S.repairExc(min(tSaEnd, nowSec() + 0.25));
                S.recomputeAll();
            }
            if (!S.isFeas()) { S.lam = min(S.lam * 1.25, lamHi); vZeroIters = 0; }
            else { S.lam = max(S.lam * 0.97, lamLo); vZeroIters++; }
            S.noteState();
        }
    }
    if (verbose) fprintf(stderr, "[sa] iters=%lld accepted=%lld F=%.6f bestFeas=%.6f (V=%.6g)\n",
                         S.iters, S.accepted, S.F(), S.bestFeasF, S.V);
    // ---- LNS phase
    if (P.lnsFrac > 0.0) {
        if (!S.bestFeas.empty()) { S.st = S.bestFeas; S.recomputeAll(); }
        else if (!S.bestAny.empty()) { S.st = S.bestAny; S.recomputeAll(); }
        double tLnsEnd = tParsed + max(1.0, (budgetEnd - tParsed) * (1.0 - P.polishFrac));
        double lTemp = max(1e-9, (P.t1frac * 4.0) * max(1e-9, S.F()));
        S.lns(tLnsEnd, lTemp, P.lnsKMin, P.lnsKMax, P.lnsCand);
        if (verbose) fprintf(stderr, "[lns] F=%.6f best=%.6f V=%.6g\n", S.F(), S.bestFeasF, S.V);
    }
    // ---- restore best feasible, then polish
    if (!S.bestFeas.empty()) { S.st = S.bestFeas; S.recomputeAll(); }
    else if (!S.bestAny.empty()) { S.st = S.bestAny; S.recomputeAll(); }
    double tPolishStop = budgetEnd - 0.3;
    while (nowSec() < tPolishStop) {
        if (!S.bestFeas.empty()) { S.st = S.bestFeas; S.recomputeAll(); }
        double before = S.bestFeasF;
        S.polish(tPolishStop, true);
        if (S.bestFeasF >= before - 1e-9) break;
    }
    if (!S.bestFeas.empty()) { S.st = S.bestFeas; S.recomputeAll(); }
    else if (!S.bestAny.empty()) { S.st = S.bestAny; S.recomputeAll(); }
    double searchEnd = nowSec();

    // ---- verify + output
    VerifyRes vr = verifySolution(I, S, S.st);
    if (outSol) writeSolution(outSol, I, S.st);
    if (verbose) {
        fprintf(stderr, "[result] feasible=%d viol=%lld (sched=%lld res=%lld exc=%lld) obj=%.9f obj1=%.9f obj2=%.9f\n",
                (int)vr.feasible, vr.viol, vr.schedViol, vr.resViol, vr.excViol, vr.obj, vr.obj1, vr.obj2);
    }
    double tEnd = nowSec();
    if (outStats) {
        char pbuf[768];
        snprintf(pbuf, sizeof(pbuf),
                 "{\"timeLimit\":%.1f,\"reserve\":%.1f,\"candCap\":%d,\"orderMode\":%d,"
                 "\"noise\":%.4f,\"restarts\":%d,\"pSingle\":%.3f,\"pSwap\":%.3f,\"pRuin\":%.3f,"
                 "\"ruinMin\":%d,\"ruinMax\":%d,\"t0frac\":%.6f,\"t1frac\":%.6f,\"polishFrac\":%.3f,"
                 "\"lamC\":%.4f,\"lamScale\":%.4f,\"lnsFrac\":%.4f,\"lnsKMin\":%d,\"lnsKMax\":%d,\"lnsCand\":%d,\"saCycles\":%d,\"restartGap\":%.4f,\"pTarget\":%.4f,\"targetCand\":%d}",
                 P.timeLimit, P.reserve, P.candCap, P.orderMode,
                 P.noise, P.restarts, P.pSingle, P.pSwap, P.pRuin,
                 P.ruinMin, P.ruinMax, P.t0frac, P.t1frac, P.polishFrac,
                 P.lamC, P.lamScale, P.lnsFrac, P.lnsKMin, P.lnsKMax, P.lnsCand,
                 P.saCycles, P.restartGap, P.pTarget, P.targetCand);
        if (outSol) {
            string tp = string(outSol) + ".trace.csv";
            FILE* tf = fopen(tp.c_str(), "wb");
            if (tf) {
                fprintf(tf, "elapsed_s,best_feasible_obj\n");
                for (auto& pr : S.trace) fprintf(tf, "%.3f,%.6f\n", pr.first, pr.second);
                fclose(tf);
            }
        }
        FILE* f = fopen(outStats, "wb");
        if (f) {
            fprintf(f,
                "{\"instance\":\"%s\",\"seed\":%llu,\"feasible\":%s,\"viol\":%lld,\"schedViol\":%lld,"
                "\"resViol\":%lld,\"excViol\":%lld,\"obj\":%.12g,\"obj1\":%.12g,\"obj2\":%.12g,"
                "\"alpha\":%.6g,\"quant\":%.6g,\"iters\":%lld,\"accepted\":%lld,\"wall\":%.3f,\"cpu\":%.3f,"
                "\"peakRssKB\":%ld,\"parseWall\":%.3f,\"searchWall\":%.3f,\"repairPasses\":%lld,"
                "\"T\":%d,\"nInt\":%d,\"nRes\":%d,\"nExc\":%d,\"maxS\":%d,\"lamFinal\":%.6g,\"params\":%s}\n",
                instPath, (unsigned long long)P.seed, vr.feasible ? "true" : "false", vr.viol, vr.schedViol,
                vr.resViol, vr.excViol, vr.obj, vr.obj1, vr.obj2,
                I.alpha, I.quant, S.iters, S.accepted, tEnd - tProcStart, cpuSeconds(),
                peakRssKB(), tParsed - tProcStart, searchEnd - tParsed, S.repairPasses,
                I.T, I.nInt, I.nRes, (int)I.excA.size(), I.maxS, S.lam, pbuf);
            fclose(f);
        }
    }
    return 0;
}
