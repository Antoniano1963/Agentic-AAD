// Engine v2 for the ROADEF2020 maintenance planning problem.
//
// Idea: the exact objective
//     F = alpha*(1/T) sum_t mean_t + (1-alpha)*(1/T) sum_t max(q_t-mean_t,0)
// has a subgradient that is piecewise constant in the per-period scenario risks:
//     dF/dR[t][s] = (2*alpha-1)/(T*S_t) + (1-alpha)/(T*k_t) * 1[s in Tail_t]
// with Tail_t the k_t = S_t - rank_t scenarios currently above the quantile.
// Fixing the tail sets turns F into a *linear* function of the assignment
// (c_i(st) = sum over the window of the weighted risk), which makes move
// evaluation O(resource records) instead of O(window * S).  We therefore
// alternate: (a) exact evaluation + tail/weight update, (b) local search on the
// linear surrogate with the capacity penalty.

struct P2 {
    double timeLimit = 840.0;
    double reserve = 8.0;
    uint64_t seed = 1;
    int    candCap = 96;
    int    orderMode = 1;
    double noise = 0.30;
    int    restarts = 3;
    double lamC = 30.0;
    double lamScale = 20.0;
    double t0frac = 0.02;
    double t1frac = 0.002;
    double cycleSeconds = 2.0;   // search time between weight updates
    double pRuin = 0.06;
    double pSwap = 0.04;
    int    ruinMin = 2;
    int    ruinMax = 8;
    int    sweepMode = 1;        // 1 = periodic best-improvement sweeps
    double repairFrac = 0.10;
    double polishFrac = 0.06;
};

struct VerifyRes {
    long long viol = 0, schedViol = 0, resViol = 0, excViol = 0;
    bool feasible = false;
    double obj1 = 0, obj2 = 0, obj = 0;
};

static void printStats2(Instance& I) {
    printf("T=%d nRes=%d nInt=%d nExc=%d maxS=%d sumS=%lld quant=%.4f alpha=%.4f rkVal=%lld wlRec=%lld\n",
           I.T, I.nRes, I.nInt, (int)I.excA.size(), I.maxS, I.sumS, I.quant, I.alpha,
           (long long)I.totalRkVal, (long long)I.totalWlRec);
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
    printf("maxUtilisation=%.4f (res %d)\n", worst, worstR);
}

struct Solver2 {
    Instance& I;
    P2 P;
    RNG rng;
    int stride;

    vector<int> st;
    vector<double> U;             // nRes*T
    double V = 0;                 // capacity penalty
    double lamE = 1.0;            // penalty per exclusion violation
    int nExc = 0;
    double csum = 0;              // linear surrogate cost of the current assignment
    double lam = 1.0;

    // weights: base[t] plus a tail bonus applied to the tail scenario indices
    vector<double> wbase;         // T
    vector<double> wbonus;        // T
    vector<vector<int>> tail;     // T -> scenario indices in the tail
    vector<int> strideTailStart;  // T+1 into tailIdx
    vector<int> tailIdx;          // concatenated tail indices

    vector<double> cslot;         // per-slot surrogate cost
    vector<double> R;             // T*stride exact risk distribution
    int maxK = 1;

    // penalty bookkeeping
    vector<int> mark, pos, idBuf;
    vector<double> dbuf;
    int stamp = 0;
    vector<double> cheaph;        // heap buffer for selection (values)
    vector<int> cheapi;

    // records
    vector<int> bestFeas;
    double bestFeasF = 1e300, bestFeasObj1 = 0, bestFeasObj2 = 0;
    vector<int> bestAny;
    double bestAnyV = 1e300;
    long long iters = 0, accepted = 0, cycles = 0, sweeps = 0;

    vector<vector<int>> cand;
    vector<int> curTailStart;

    Solver2(Instance& inst, P2 p) : I(inst), P(p), rng(p.seed) {
        stride = I.maxS;
        st.assign(I.nInt, 0);
        U.assign((size_t)I.nRes * I.T + 1, 0.0);
        wbase.assign(I.T + 1, 0.0);
        wbonus.assign(I.T + 1, 0.0);
        tail.assign(I.T + 1, {});
        strideTailStart.assign(I.T + 2, 0);
        cslot.assign(I.slotBase[I.nInt], 0.0);
        R.assign((size_t)I.T * stride + stride, 0.0);
        mark.assign((size_t)I.nRes * I.T + 1, 0);
        pos.assign((size_t)I.nRes * I.T + 1, 0);
        cheaph.assign(stride + 2, 0.0);
        cheapi.assign(stride + 2, 0);
        idBuf.reserve(4096);
        dbuf.reserve(4096);
        buildCandidates();
    }

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
            }
            cand[i] = std::move(s);
        }
    }

    // ---------------------------------------------------------------- penalty
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
        for (int k = 0; k < cnt; k++) {
            size_t id = (size_t)idBuf[k];
            double b = U[id], a = b + dbuf[k];
            d += penCell(id, a) - penCell(id, b);
        }
        dropCells(cnt);
        return d;
    }

    inline bool exclConflict(int i, int sti, int j, int stj, int sea) const {
        int di = I.deltaAt(i, sti), dj = I.deltaAt(j, stj);
        int lo = max(sti, stj), hi = min(sti + di, stj + dj);
        if (lo >= hi) return false;
        const int* pf = &I.seaPref[(size_t)sea * (I.T + 2)];
        return (pf[hi - 1] - pf[lo - 1]) > 0;
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
    bool pairOk(int i, int sti, int j, int stj) {
        for (i64 a = I.adjStart[i]; a < I.adjStart[i + 1]; a++)
            if (I.adjJ[a] == j && exclConflict(i, sti, j, stj, I.adjSea[a])) return false;
        return true;
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

    // apply i: stOld -> stNew (0 = remove), updating U/V/nExc/csum
    void moveTo(int i, int stNew) {
        int stOld = st[i];
        if (stOld == stNew) return;
        updExcCount(i, stOld, stNew);
        int cnt = buildCellDelta(i, stOld, stNew);
        for (int k = 0; k < cnt; k++) {
            size_t id = (size_t)idBuf[k];
            double b = U[id], a = b + dbuf[k];
            if (a != b) { V += penCell(id, a) - penCell(id, b); U[id] = a; }
        }
        dropCells(cnt);
        if (stOld > 0) csum -= cslot[I.slot(i, stOld)];
        if (stNew > 0) csum += cslot[I.slot(i, stNew)];
        st[i] = stNew;
    }

    // ---------------------------------------------------------------- exact evaluation + weights
    // rebuild R from st, compute exact F (and optionally store the tail sets)
    double exactEval(bool updateWeights) {
        fill(R.begin(), R.end(), 0.0);
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
        }
        double o1 = 0, o2 = 0;
        for (int t = 0; t < I.T; t++) {
            const int S = I.S[t];
            double* row = &R[(size_t)t * stride];
            double sum = 0.0;
            for (int s = 0; s < S; s++) sum += row[s];
            double m = sum / (double)S;
            int rank = (int)ceil((double)S * I.quant) - 1;
            if (rank < 0) rank = 0;
            if (rank >= S) rank = S - 1;
            // select the k=(S-rank) largest with indices
            int k = S - rank;
            double* hv = cheaph.data();
            int* hi = cheapi.data();
            for (int a = 0; a < k; a++) { hv[a] = row[a]; hi[a] = a; }
            for (int a = (k >> 1) - 1; a >= 0; a--) siftMinIdx(hv, hi, k, a);
            for (int a = k; a < S; a++) {
                if (row[a] > hv[0]) { hv[0] = row[a]; hi[0] = a; siftMinIdx(hv, hi, k, 0); }
            }
            double q = hv[0];
            o1 += m;
            o2 += max(q - m, 0.0);
            if (updateWeights) {
                double base = (2.0 * I.alpha - 1.0) / ((double)I.T * (double)S);
                double bonus = (1.0 - I.alpha) / ((double)I.T * (double)k);
                wbase[t] = base;
                wbonus[t] = bonus;
                tail[t].assign(hi, hi + k);
                sort(tail[t].begin(), tail[t].end());
            }
        }
        // NOTE: F uses alpha*obj1 + (1-alpha)*obj2 with obj1=(1/T)sum m, o2 similarly
        double F = I.alpha * (o1 / (double)I.T) + (1.0 - I.alpha) * (o2 / (double)I.T);
        return F;
    }

    static inline void siftMinIdx(double* hv, int* hi, int n, int a) {
        double v = hv[a];
        int iv = hi[a];
        while (true) {
            int c = 2 * a + 1;
            if (c >= n) break;
            if (c + 1 < n && hv[c + 1] < hv[c]) c++;
            if (hv[c] >= v) break;
            hv[a] = hv[c]; hi[a] = hi[c]; a = c;
        }
        hv[a] = v; hi[a] = iv;
    }

    // recompute per-slot surrogate costs from the current weights
    void recomputeSlotCosts() {
        double t0 = nowSec();
        int slots = I.slotBase[I.nInt];
        for (int sl = 0; sl < slots; sl++) {
            if (!I.okStart[sl]) { cslot[sl] = 1e300; continue; }
            double acc = 0.0;
            for (i64 k = I.rkStart[sl]; k < I.rkStart[sl + 1]; k++) {
                int t = I.rkRecT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                const double* v = &I.rkVal[I.rkRecOff[k]];
                const int S = I.S[t];
                double b = wbase[t];
                if (b != 0.0) {
                    double s = 0.0;
                    for (int q = 0; q < S; q++) s += v[q];
                    acc += b * s;
                }
                double bo = wbonus[t];
                if (bo != 0.0) {
                    double s = 0.0;
                    for (int idx : tail[t]) s += v[idx];
                    acc += bo * s;
                }
            }
            cslot[sl] = acc;
        }
        (void)t0;
    }

    void recomputeCsum() {
        csum = 0.0;
        for (int i = 0; i < I.nInt; i++) if (st[i] > 0) csum += cslot[I.slot(i, st[i])];
    }

    // ---------------------------------------------------------------- construction / repair
    vector<int> makeOrder(int mode, double noiseScale) {
        vector<int> ord(I.nInt);
        iota(ord.begin(), ord.end(), 0);
        if (mode == 0) {
            for (int a = I.nInt - 1; a > 0; a--) swap(ord[a], ord[rng.i01(a + 1)]);
        } else if (mode == 1) {
            vector<pair<double, int>> key(I.nInt);
            for (int i = 0; i < I.nInt; i++) {
                i64 sl = I.slotBase[i];
                double wl = (double)(I.wlStart[sl + 1] - I.wlStart[sl]);
                int d = (sl < (int)I.delta.size() && I.delta[sl] > 0) ? I.delta[sl] : 1;
                key[i] = {-wl / (double)d * (double)d, i};
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

    void construct(const vector<int>& ord, double noiseScale, double lamC) {
        fill(st.begin(), st.end(), 0);
        fill(U.begin(), U.end(), 0.0);
        V = 0; nExc = 0; csum = 0;
        for (int i : ord) {
            int hi = I.slotHi[i];
            if (hi <= 0) { st[i] = 1; continue; }
            int bs = -1;
            double best = 1e300;
            for (int stt : cand[i]) {
                if (!exclOk(i, stt)) continue;
                double sc = cslot[I.slot(i, stt)] * (1.0 + noiseScale * rng.u01()) + lamC * dVOf(i, 0, stt);
                if (sc < best) { best = sc; bs = stt; }
            }
            if (bs < 0) {
                for (int stt = 1; stt <= hi; stt++) {
                    if (!I.okStart[I.slot(i, stt)]) continue;
                    double sc = cslot[I.slot(i, stt)] + lamC * dVOf(i, 0, stt);
                    if (sc < best) { best = sc; bs = stt; }
                }
            }
            if (bs < 0) bs = cand[i].empty() ? 1 : cand[i][0];
            // place
            updExcCount(i, 0, bs);
            int cnt = buildCellDelta(i, 0, bs);
            for (int k = 0; k < cnt; k++) {
                size_t id = (size_t)idBuf[k];
                double b = U[id], a = b + dbuf[k];
                V += penCell(id, a) - penCell(id, b);
                U[id] = a;
            }
            dropCells(cnt);
            st[i] = bs;
            csum += cslot[I.slot(i, bs)];
        }
    }

    double recomputeV() {
        V = 0;
        for (size_t id = 0; id < (size_t)I.nRes * I.T; id++) V += penCell(id, U[id]);
        nExc = 0;
        for (size_t k = 0; k < I.excA.size(); k++) {
            int a = I.excA[k], b = I.excB[k];
            if (st[a] > 0 && st[b] > 0 && exclConflict(a, st[a], b, st[b], I.excSea[k])) nExc++;
        }
        return V;
    }

    long long countViolCells() const {
        long long v = 0;
        for (size_t id = 0; id < (size_t)I.nRes * I.T; id++)
            if (U[id] > I.rmax[id] + 1e-6 || U[id] < I.rmin[id] - 1e-6) v++;
        return v;
    }

    vector<double> cellW;
    bool useW = false;
    inline double penCellW(int id, double u) const {
        double p = 0.0;
        double ov = u - I.rmax[id];
        if (ov > 0) p += ov * ov;
        double un = I.rmin[id] - u;
        if (un > 0) p += un * un;
        return useW ? p * cellW[id] : p;
    }
    double dVOfW(int i, int stOld, int stNew) {
        int cnt = buildCellDelta(i, stOld, stNew);
        double d = 0.0;
        for (int k = 0; k < cnt; k++) {
            int id = idBuf[k];
            double b = U[id], a = b + dbuf[k];
            d += penCellW(id, a) - penCellW(id, b);
        }
        dropCells(cnt);
        return d;
    }

    // number of partners that would conflict with intervention i at start stt
    int confCount(int i, int stt) {
        int c = 0;
        for (i64 a = I.adjStart[i]; a < I.adjStart[i + 1]; a++) {
            int j = I.adjJ[a];
            int sj = st[j];
            if (sj <= 0 || j == i) continue;
            if (exclConflict(i, stt, j, sj, I.adjSea[a])) c++;
        }
        return c;
    }

    // min-conflicts + random walk over the exclusion graph
    bool repairExc(double tStop) {
        if (nExc == 0) return true;
        vector<size_t> conf;
        for (int step = 0; step < 60000; step++) {
            if ((step & 31) == 0 && nowSec() > tStop) return nExc == 0;
            conf.clear();
            for (size_t k = 0; k < I.excA.size(); k++) {
                int a = I.excA[k], b = I.excB[k];
                if (st[a] <= 0 || st[b] <= 0) continue;
                if (exclConflict(a, st[a], b, st[b], I.excSea[k])) conf.push_back(k);
            }
            if (conf.empty()) return true;
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
            if (bs == cur || rng.u01() < 0.25) {
                int stt = (hi <= 512) ? (1 + rng.i01(hi)) : cand[i][rng.i01((int)cand[i].size())];
                if (stt == cur || !I.okStart[I.slot(i, stt)]) continue;
                bs = stt;
            }
            moveTo(i, bs);
        }
        return nExc == 0;
    }

    bool repair(double tStop) {
        if (nExc > 0) repairExc(tStop);
        cellW.assign((size_t)I.nRes * I.T, 1.0);
        useW = true;
        vector<char> badCell((size_t)I.nRes * I.T, 0);
        vector<int> dirty, badList;
        int stall = 0;
        bool res = false;
        for (int pass = 0; pass < 200000; pass++) {
            if (nowSec() > tStop) { res = isFeas(); break; }
            bool anyBad = false;
            fill(badCell.begin(), badCell.end(), 0);
            for (size_t id = 0; id < (size_t)I.nRes * I.T; id++) {
                double u = U[id];
                if (u > I.rmax[id] + 1e-6 || u < I.rmin[id] - 1e-6) { badCell[id] = 1; anyBad = true; }
            }
            if (!anyBad && nExc == 0) { res = true; break; }
            badList.clear();
            for (size_t id = 0; id < (size_t)I.nRes * I.T; id++) if (badCell[id]) badList.push_back((int)id);
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
            if (dirty.empty()) { for (int i = 0; i < I.nInt; i++) dirty.push_back(i); }
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
                    if (!I.okStart[I.slot(i, stt)] || !exclOk(i, stt)) continue;
                    double d = dVOfW(i, cur, stt);
                    if (d < bestD - 1e-15) { bestD = d; bs = stt; }
                }
                if (nAll == 0) {
                    for (int stt : cand[i]) {
                        if (stt == cur) continue;
                        if (!exclOk(i, stt)) continue;
                        double d = dVOfW(i, cur, stt);
                        if (d < bestD - 1e-15) { bestD = d; bs = stt; }
                    }
                }
                if (bs != cur) { moveTo(i, bs); improvements++; }
            }
            if (isFeas()) { res = true; break; }
            if (improvements == 0) {
                stall++;
                for (int id : badList) cellW[id] += 1.0;
                if (stall > 4000) { res = isFeas(); break; }
                for (int tries = 0; tries < 4 && !dirty.empty(); tries++) {
                    int i = dirty[rng.i01((int)dirty.size())];
                    int hi = I.slotHi[i];
                    if (hi <= 0) continue;
                    int stt = 1 + rng.i01(hi);
                    if (!I.okStart[I.slot(i, stt)] || !exclOk(i, stt)) continue;
                    if (stt != st[i]) moveTo(i, stt);
                }
            } else stall = 0;
        }
        useW = false;
        return res;
    }

    // ---------------------------------------------------------------- local search on the surrogate
    void noteState(double exactF) {
        if (isFeas()) {
            if (exactF < bestFeasF) {
                bestFeasF = exactF;
                bestFeas = st;
            }
        }
        if (V < bestAnyV) { bestAnyV = V; bestAny = st; }
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
        if (u < 0.70) stNew = pickCand(i);
        else if (u < 0.90) { int d = 1 + rng.i01(3); stNew = stOld + ((rng.u01() < 0.5) ? -d : d); }
        else stNew = 1 + rng.i01(I.slotHi[i]);
        if (stNew == stOld || stNew < 1 || stNew > I.slotHi[i] || !I.okStart[I.slot(i, stNew)]) return;
        if (!exclOk(i, stNew)) return;
        iters++;
        double dC = cslot[I.slot(i, stNew)] - cslot[I.slot(i, stOld)];
        double dV = dVOf(i, stOld, stNew);
        double d = dC + lam * dV;
        if (d <= 0.0 || rng.u01() < exp(-d / temp)) {
            accepted++;
            moveTo(i, stNew);
        }
    }

    void swapMove(double temp) {
        int i = rng.i01(I.nInt), j = rng.i01(I.nInt);
        if (i == j) return;
        int si = st[i], sj = st[j];
        if (si <= 0 || sj <= 0) return;
        if (sj > I.slotHi[i] || si > I.slotHi[j]) return;
        if (!I.okStart[I.slot(i, sj)] || !I.okStart[I.slot(j, si)]) return;
        int a0 = st[i], b0 = st[j];
        st[i] = 0; st[j] = 0;
        bool ok = exclOk(i, sj) && exclOk(j, si) && pairOk(i, sj, j, si);
        st[i] = a0; st[j] = b0;
        if (!ok) return;
        iters++;
        // approximate delta by summing both single moves (ignoring their interaction on shared cells)
        double d = cslot[I.slot(i, sj)] - cslot[I.slot(i, si)]
                 + cslot[I.slot(j, si)] - cslot[I.slot(j, sj)]
                 + lam * (dVOf(i, si, sj) + dVOf(j, sj, si));
        if (d <= 0.0 || rng.u01() < exp(-d / temp)) {
            accepted++;
            moveTo(i, sj);
            moveTo(j, si);
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
        double c0 = csum + lam * V;
        for (int a = 0; a < k; a++) moveTo(chosen[a], 0);
        bool okAll = true;
        for (int a = 0; a < k; a++) {
            int c = chosen[a];
            int bs = -1;
            double best = 1e300;
            for (int stt : cand[c]) {
                if (!exclOk(c, stt)) continue;
                double sc = cslot[I.slot(c, stt)] + lam * dVOf(c, 0, stt) + 1e-9 * rng.u01();
                if (sc < best) { best = sc; bs = stt; }
            }
            if (bs < 0) { okAll = false; break; }
            moveTo(c, bs);
        }
        if (!okAll) {
            for (int a = 0; a < k; a++) if (st[chosen[a]] > 0) moveTo(chosen[a], 0);
            for (int a = 0; a < k; a++) if (saved[a] > 0) moveTo(chosen[a], saved[a]);
            return;
        }
        double d = (csum + lam * V) - c0;
        if (d <= 0.0 || rng.u01() < exp(-d / temp)) accepted++;
        else {
            for (int a = 0; a < k; a++) if (st[chosen[a]] > 0) moveTo(chosen[a], 0);
            for (int a = 0; a < k; a++) if (saved[a] > 0) moveTo(chosen[a], saved[a]);
        }
    }

    // best-improvement sweep over interventions (surrogate + penalty)
    long long sweep(double temp) {
        vector<int> ord(I.nInt);
        iota(ord.begin(), ord.end(), 0);
        for (int a = I.nInt - 1; a > 0; a--) swap(ord[a], ord[rng.i01(a + 1)]);
        long long improved = 0;
        for (int i : ord) {
            int hi = I.slotHi[i];
            if (hi <= 0) continue;
            int cur = st[i];
            double bestD = -1e-12;
            int bs = cur;
            for (int stt : cand[i]) {
                if (stt == cur) continue;
                if (!exclOk(i, stt)) continue;
                double d = cslot[I.slot(i, stt)] - cslot[I.slot(i, cur)] + lam * dVOf(i, cur, stt);
                if (d < bestD) { bestD = d; bs = stt; }
            }
            if (bs != cur) { moveTo(i, bs); improved++; }
        }
        sweeps++;
        return improved;
    }

    // ---------------------------------------------------------------- main loop
    void run(double budgetEnd, double tParsed, bool verbose) {
        // initial linear cost uses cArr (unary cost is the alpha-part surrogate)
        for (int sl = 0; sl < I.slotBase[I.nInt]; sl++) cslot[sl] = I.okStart[sl] ? I.cArr[sl] : 1e300;
        double tRepairStop = min(tParsed + (budgetEnd - tParsed) * P.repairFrac, budgetEnd - 0.5);
        double lamC = P.lamC;
        bool feas = false;
        for (int attempt = 0; attempt < P.restarts; attempt++) {
            int mode = (attempt == 0) ? P.orderMode : (P.orderMode + attempt) % 4;
            vector<int> ord = makeOrder(mode, P.noise);
            construct(ord, P.noise, lamC);
            if (verbose) fprintf(stderr, "[construct] att=%d mode=%d viol=%lld\n", attempt, mode, countViolCells());
            if (isFeas()) { feas = true; break; }
            if (repair(min(tRepairStop, nowSec() + 20.0))) { feas = true; break; }
            if (nowSec() > tRepairStop) break;
            lamC *= 3.0;
        }
        if (!feas) feas = repair(tRepairStop);
        if (verbose) fprintf(stderr, "[phase1] feas=%d violCells=%lld V=%.6g\n", (int)feas, countViolCells(), V);

        // exact evaluation and weights
        double F = exactEval(true);
        recomputeSlotCosts();
        recomputeCsum();
        recomputeV();
        double fnow = max(1e-9, F);
        lam = max(1.0, P.lamScale * fnow);
        lamE = 1e6 * fnow;
        double lamLo = lam * 1e-2, lamHi = lam * 1e2;
        noteState(F);
        if (verbose) fprintf(stderr, "[init] F=%.6f csum=%.6f lam=%.4g violCells=%lld\n", F, csum, lam, countViolCells());

        double tSaEnd = budgetEnd;
        double Tstart = P.t0frac * max(1e-9, fabs(csum) / max(1, I.nInt));
        double Tend = P.t1frac * max(1e-9, fabs(csum) / max(1, I.nInt));
        if (Tstart <= 0) Tstart = 1e-6;
        if (Tend <= 0) Tend = 1e-9;
        int cycle = 0;
        while (nowSec() < tSaEnd) {
            cycles++;
            cycle++;
            double frac = 1.0 - (tSaEnd - nowSec()) / max(1e-9, (tSaEnd - tParsed));
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            double temp = Tstart * pow(Tend / Tstart, frac);
            // ---- inner search on the surrogate
            double tInner = min(tSaEnd, nowSec() + P.cycleSeconds);
            long long moves = 0;
            while (nowSec() < tInner) {
                for (int rep = 0; rep < 256; rep++) {
                    double u = rng.u01();
                    if (u < P.pRuin) ruinRecreate(temp);
                    else if (u < P.pRuin + P.pSwap) swapMove(temp);
                    else oneMove(temp);
                }
                moves += 256;
                if ((cycle & 1) == 0 && (moves % 100000) == 0) sweep(temp);
            }
            // ---- exact evaluation + weight update
            if (nExc > 0) repairExc(min(tSaEnd, nowSec() + 0.3));
            double Fc = exactEval(true);
            recomputeSlotCosts();
            rebuildStateFromSt();   // exact U/V/nExc/csum from st (removes drift)
            if (isFeas() && Fc < bestFeasF) { bestFeasF = Fc; bestFeas = st; }
            if (V < bestAnyV) { bestAnyV = V; bestAny = st; }
            if (verbose && (cycle % 20) == 0)
                fprintf(stderr, "[cycle %d] F=%.6f best=%.6f viol=%lld iters=%lld\n",
                        cycle, Fc, bestFeasF, countViolCells(), iters);
            // adapt the penalty weight
            if (!isFeas()) lam = min(lam * 1.3, lamHi);
            else lam = max(lam * 0.98, lamLo);
        }
        if (verbose) fprintf(stderr, "[sa] cycles=%lld iters=%lld accepted=%lld sweeps=%lld F=%.6f best=%.6f\n",
                             cycles, iters, accepted, sweeps, exactEval(false), bestFeasF);
        // ---- restore best and final polish (surrogate + exact accept)
        if (!bestFeas.empty()) st = bestFeas;
        else if (!bestAny.empty()) st = bestAny;
        rebuildStateFromSt();
        double tPolishStop = budgetEnd - 0.2;
        for (int round = 0; round < 6 && nowSec() < tPolishStop; round++) {
            double before = bestFeasF;
            exactEval(true);
            recomputeSlotCosts();
            recomputeCsum();
            recomputeV();
            long long imp = sweep(1e-9);
            double Fc = exactEval(false);
            if (isFeas() && Fc < bestFeasF) { bestFeasF = Fc; bestFeas = st; }
            if (imp == 0 || bestFeasF >= before - 1e-9) break;
            if (nowSec() > tPolishStop) break;
        }
        if (!bestFeas.empty()) st = bestFeas;
        else if (!bestAny.empty()) st = bestAny;
    }

    void rebuildStateFromSt() {
        fill(U.begin(), U.end(), 0.0);
        for (int i = 0; i < I.nInt; i++) {
            if (st[i] <= 0) continue;
            int sl = I.slot(i, st[i]);
            const int ws = st[i], we = st[i] + I.delta[sl] - 1;
            for (i64 k = I.wlStart[sl]; k < I.wlStart[sl + 1]; k++) {
                int t = I.wlT[k] - 1;
                if (t < 0 || t >= I.T) continue;
                if (t + 1 < ws || t + 1 > we) continue;
                U[(size_t)I.wlRes[k] * I.T + t] += (double)I.wlVal[k];
            }
        }
        recomputeV();
        recomputeCsum();
    }
};
