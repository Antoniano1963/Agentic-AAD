// Engine v2 entry point: ROADEF2020 from-scratch solver (linearized tail surrogate)
#include "inst.inc"
#include "solver2_part.inc"

static void writeSolution2(const char* path, const Instance& I, const vector<int>& sol) {
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

static VerifyRes verifySolution2(Instance& I, const vector<int>& sol) {
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
    auto conflictof = [&](int a, int sa, int b, int sb, int sea) {
        int da = I.deltaAt(a, sa), db = I.deltaAt(b, sb);
        int lo = max(sa, sb), hi = min(sa + da, sb + db);
        if (lo >= hi) return false;
        const int* pf = &I.seaPref[(size_t)sea * (I.T + 2)];
        return (pf[hi - 1] - pf[lo - 1]) > 0;
    };
    v.excViol = 0;
    for (size_t k = 0; k < I.excA.size(); k++) {
        int a = I.excA[k], b = I.excB[k];
        int sa = sol[a], sb = sol[b];
        if (sa < 1 || sb < 1) { v.excViol++; continue; }
        if (conflictof(a, sa, b, sb, I.excSea[k])) v.excViol++;
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
    vector<double> hv((size_t)stride + 2), h2((size_t)stride + 2);
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
        memcpy(hv.data(), row, sizeof(double) * (size_t)Sn);
        nth_element(hv.data(), hv.data() + rank, hv.data() + Sn);
        double q = hv[rank];
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
    P2 P;
    const char* instPath = nullptr;
    const char* outSol = nullptr;
    const char* outStats = nullptr;
    bool verbose = false, statsOnly = false;
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
        else if (a == "--lamc") P.lamC = atof(nextv());
        else if (a == "--lamscale") P.lamScale = atof(nextv());
        else if (a == "--t0") P.t0frac = atof(nextv());
        else if (a == "--t1") P.t1frac = atof(nextv());
        else if (a == "--cycle") P.cycleSeconds = atof(nextv());
        else if (a == "--pruin") P.pRuin = atof(nextv());
        else if (a == "--pswap") P.pSwap = atof(nextv());
        else if (a == "--ruinmin") P.ruinMin = atoi(nextv());
        else if (a == "--ruinmax") P.ruinMax = atoi(nextv());
        else if (a == "--repairfrac") P.repairFrac = atof(nextv());
        else { fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (!instPath) { fprintf(stderr, "need --instance\n"); return 2; }
    Instance I;
    if (!I.parse(instPath, verbose)) return 2;
    I.finalize(verbose);
    double tParsed = nowSec();
    if (statsOnly) { printStats2(I); return 0; }

    double budgetEnd = tProcStart + P.timeLimit - P.reserve;
    if (budgetEnd < tParsed + 1.0) budgetEnd = tParsed + 1.0;
    Solver2 S(I, P);
    S.run(budgetEnd, tParsed, verbose);
    double searchEnd = nowSec();
    vector<int> finalSt = S.st;
    VerifyRes vr = verifySolution2(I, finalSt);
    if (outSol) writeSolution2(outSol, I, finalSt);
    if (verbose)
        fprintf(stderr, "[result] feasible=%d viol=%lld (sched=%lld res=%lld exc=%lld) obj=%.9f obj1=%.9f obj2=%.9f\n",
                (int)vr.feasible, vr.viol, vr.schedViol, vr.resViol, vr.excViol, vr.obj, vr.obj1, vr.obj2);
    double tEnd = nowSec();
    if (outStats) {
        char pbuf[512];
        snprintf(pbuf, sizeof(pbuf),
                 "{\"timeLimit\":%.1f,\"reserve\":%.1f,\"candCap\":%d,\"orderMode\":%d,\"noise\":%.4f,"
                 "\"restarts\":%d,\"lamC\":%.4f,\"lamScale\":%.4f,\"t0frac\":%.6f,\"t1frac\":%.6f,"
                 "\"cycleSeconds\":%.3f,\"pRuin\":%.4f,\"pSwap\":%.4f,\"ruinMin\":%d,\"ruinMax\":%d}",
                 P.timeLimit, P.reserve, P.candCap, P.orderMode, P.noise,
                 P.restarts, P.lamC, P.lamScale, P.t0frac, P.t1frac,
                 P.cycleSeconds, P.pRuin, P.pSwap, P.ruinMin, P.ruinMax);
        FILE* f = fopen(outStats, "wb");
        if (f) {
            fprintf(f,
                    "{\"instance\":\"%s\",\"engine\":\"v2\",\"seed\":%llu,\"feasible\":%s,\"viol\":%lld,"
                    "\"schedViol\":%lld,\"resViol\":%lld,\"excViol\":%lld,\"obj\":%.12g,\"obj1\":%.12g,"
                    "\"obj2\":%.12g,\"alpha\":%.6g,\"quant\":%.6g,\"iters\":%lld,\"accepted\":%lld,"
                    "\"cycles\":%lld,\"sweeps\":%lld,\"wall\":%.3f,\"cpu\":%.3f,\"peakRssKB\":%ld,"
                    "\"parseWall\":%.3f,\"searchWall\":%.3f,\"T\":%d,\"nInt\":%d,\"nRes\":%d,\"nExc\":%d,"
                    "\"maxS\":%d,\"params\":%s}\n",
                    instPath, (unsigned long long)P.seed, vr.feasible ? "true" : "false", vr.viol,
                    vr.schedViol, vr.resViol, vr.excViol, vr.obj, vr.obj1, vr.obj2,
                    I.alpha, I.quant, S.iters, S.accepted, S.cycles, S.sweeps,
                    tEnd - tProcStart, cpuSeconds(), peakRssKB(), tParsed - tProcStart,
                    searchEnd - tParsed, I.T, I.nInt, I.nRes, (int)I.excA.size(), I.maxS, pbuf);
            fclose(f);
        }
    }
    return 0;
}
