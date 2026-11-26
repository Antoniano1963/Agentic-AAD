// ROADEF2020 (RTE) maintenance planning problem - from-scratch solver.
// Single file: JSON parsing, exact objective evaluation, constructive heuristic, local search.
//
// Build: g++ -O2 -march=native -std=c++17 -o bin/roadef src/solver.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <unordered_map>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>

using namespace std;
using i32 = int32_t;
using i64 = int64_t;

static double nowSec() {
    using namespace chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static double POW10[40], POW10N[40];
static void initPow() {
    double p = 1.0;
    for (int i = 0; i < 40; i++) { POW10[i] = p; POW10N[i] = 1.0 / p; p *= 10.0; }
}

// ------------------------------------------------------------------ mapped file
struct MFile {
    const char* d = nullptr;
    size_t n = 0;
    bool open(const char* path) {
        int fd = ::open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) != 0) { ::close(fd); return false; }
        n = (size_t)st.st_size;
        if (n == 0) { ::close(fd); return false; }
        void* m = mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m == MAP_FAILED) { ::close(fd); return false; }
        d = (const char*)m;
        ::close(fd);
        return true;
    }
};

// ------------------------------------------------------------------ JSON scanner
struct JScan {
    const char* p;
    const char* e;

    inline void ws() {
        while (p < e) {
            char c = *p;
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') ++p; else break;
        }
    }
    inline void need(char c) {
        ws();
        if (p >= e || *p != c) {
            fprintf(stderr, "JSON error: expected '%c' near: %.60s\n", c, p);
            exit(3);
        }
        ++p;
    }
    inline const char* str(size_t& len) {
        ws();
        if (p < e && *p == '"') ++p;
        const char* s = p;
        while (p < e && *p != '"') ++p;
        len = (size_t)(p - s);
        if (p < e) ++p;
        return s;
    }
    inline void skipValue() {
        ws();
        if (p >= e) return;
        char c = *p;
        if (c == '"') { ++p; while (p < e && *p != '"') { if (*p == '\\') ++p; ++p; } if (p < e) ++p; return; }
        if (c == '{' || c == '[') {
            int depth = 0;
            while (p < e) {
                char d = *p;
                if (d == '"') { ++p; while (p < e && *p != '"') { if (*p == '\\') ++p; ++p; } if (p < e) ++p; continue; }
                if (d == '{' || d == '[') ++depth;
                else if (d == '}' || d == ']') { --depth; if (depth == 0) { ++p; return; } }
                ++p;
            }
        }
        while (p < e) {
            char d = *p;
            if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\n' || d == '\r' || d == '\t') break;
            ++p;
        }
    }
    inline double num() {
        ws();
        bool quoted = false;
        if (p < e && *p == '"') { quoted = true; ++p; }
        bool neg = false;
        if (p < e && (*p == '-' || *p == '+')) { neg = (*p == '-'); ++p; }
        uint64_t m = 0;
        int nd = 0;
        while (p < e) { char c = *p; if (c < '0' || c > '9') break; m = m * 10 + (uint64_t)(c - '0'); ++nd; ++p; }
        int ex = 0;
        if (p < e && *p == '.') {
            ++p;
            while (p < e) { char c = *p; if (c < '0' || c > '9') break; m = m * 10 + (uint64_t)(c - '0'); ++nd; --ex; ++p; }
        }
        if (p < e && (*p == 'e' || *p == 'E')) {
            ++p;
            bool en = false;
            if (p < e && (*p == '-' || *p == '+')) { en = (*p == '-'); ++p; }
            int ev = 0;
            while (p < e && *p >= '0' && *p <= '9') { ev = ev * 10 + (*p - '0'); ++p; }
            ex += en ? -ev : ev;
        }
        if (quoted) { while (p < e && *p != '"') ++p; if (p < e) ++p; }
        if (nd == 0) return 0.0;
        double d = (double)m;
        // exact powers of ten up to 1e22 are representable, so multiply/divide by them
        // gives the correctly rounded decimal->binary conversion (1 ulp matters for the
        // quantile rank, e.g. 120*0.95 must be exactly 114.0)
        if (ex > 0) { d = (ex <= 22) ? d * POW10[ex] : d * pow(10.0, ex); }
        else if (ex < 0) { int ax = -ex; d = (ax <= 22) ? d / POW10[ax] : d / pow(10.0, ax); }
        return neg ? -d : d;
    }
    inline i64 inum() {
        ws();
        bool quoted = false;
        if (p < e && *p == '"') { quoted = true; ++p; }
        bool neg = false;
        if (p < e && (*p == '-' || *p == '+')) { neg = (*p == '-'); ++p; }
        i64 v = 0;
        while (p < e && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        if (p < e && *p == '.') { ++p; while (p < e && *p >= '0' && *p <= '9') ++p; }
        if (quoted) { while (p < e && *p != '"') ++p; if (p < e) ++p; }
        return neg ? -v : v;
    }
};

static inline int keyInt(const char* s, size_t len) {
    int v = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c < '0' || c > '9') break;
        v = v * 10 + (c - '0');
    }
    return v;
}
static bool nameEq(const char* s, size_t len, const char* t) {
    size_t n = strlen(t);
    return len == n && memcmp(s, t, len) == 0;
}

// ------------------------------------------------------------------ Instance
struct Instance {
    int T = 0;
    int nRes = 0;
    int nInt = 0;
    vector<string> resName;
    vector<double> rmin, rmax;      // nRes * T
    int nSea = 0;
    vector<string> seaName;
    vector<int> seaPref;            // nSea * (T+2)

    vector<int> tmax;               // per intervention
    vector<int> slotBase;           // nInt+1
    vector<int> delta;              // per slot
    vector<uint8_t> okStart;        // per slot
    vector<int> slotHi;             // per intervention (0 if none)

    vector<i64> wlStart;            // slots+1
    vector<i32> wlRes, wlT;
    vector<double> wlVal;

    vector<i64> rkStart;            // slots+1
    vector<i32> rkRecT;
    vector<i64> rkRecOff;
    vector<double> rkVal;
    vector<double> rkRecSum;
    vector<double> cArr;            // unary objective contribution per slot

    vector<int> S;                  // scenario count per period (1-based -> S[t-1])
    int maxS = 1;
    long long sumS = 0;
    double quant = 0.95;
    double alpha = 0.5;

    vector<int> excA, excB, excSea;
    vector<i64> adjStart;           // nInt+1
    vector<i32> adjJ, adjSea;

    vector<string> intName;
    i64 totalWlRec = 0, totalRkRec = 0, totalRkVal = 0;

    inline int slot(int i, int st) const { return slotBase[i] + st - 1; }
    inline int deltaAt(int i, int st) const { return delta[slotBase[i] + st - 1]; }
    bool parse(const char* path, bool verbose);
    void finalize(bool verbose);
};

// ------------------------------------------------------------------ parser
struct TWl { int st; int res; int t; double v; };
struct TRk { int st; int t; i64 off; double sum; };

bool Instance::parse(const char* path, bool verbose) {
    MFile f;
    if (!f.open(path)) { fprintf(stderr, "cannot open instance %s\n", path); return false; }
    JScan js{f.d, f.d + f.n};
    double t0 = nowSec();

    vector<string> resNameTmp;
    vector<vector<double>> resMinT, resMaxT;
    vector<string> seaNameTmp;
    vector<vector<int>> seaPerT;
    vector<string> ex1, ex2;
    vector<int> exSea;
    vector<string> intNameTmp;
    vector<int> tmaxTmp;
    vector<int> tDeltaBuf;
    vector<TWl> tmpWl;
    vector<TRk> tmpRk;
    vector<int> stCnt;
    vector<double> Sbuf;

    auto resId = [&](const char* s, size_t len) -> int {
        for (int k = 0; k < (int)resNameTmp.size(); k++)
            if (nameEq(s, len, resNameTmp[k].c_str())) return k;
        resNameTmp.push_back(string(s, len));
        resMinT.resize(resNameTmp.size());
        resMaxT.resize(resNameTmp.size());
        return (int)resNameTmp.size() - 1;
    };
    auto seaId = [&](const char* s, size_t len) -> int {
        for (int k = 0; k < (int)seaNameTmp.size(); k++)
            if (nameEq(s, len, seaNameTmp[k].c_str())) return k;
        seaNameTmp.push_back(string(s, len));
        seaPerT.resize(seaNameTmp.size());
        return (int)seaNameTmp.size() - 1;
    };

    js.need('{');
    while (true) {
        js.ws();
        if (js.p < js.e && *js.p == '}') { ++js.p; break; }
        if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
        size_t klen; const char* kp = js.str(klen);
        js.need(':');

        if (nameEq(kp, klen, "Resources")) {
            js.need('{');
            while (true) {
                js.ws();
                if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                size_t nl; const char* np = js.str(nl);
                js.need(':');
                int rid = resId(np, nl);
                js.need('{');
                while (true) {
                    js.ws();
                    if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                    if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                    size_t fl; const char* fp = js.str(fl);
                    js.need(':');
                    js.need('[');
                    vector<double> vals;
                    while (true) {
                        js.ws();
                        if (js.p < js.e && *js.p == ']') { ++js.p; break; }
                        if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                        vals.push_back(js.num());
                    }
                    if (nameEq(fp, fl, "min")) resMinT[rid] = std::move(vals);
                    else if (nameEq(fp, fl, "max")) resMaxT[rid] = std::move(vals);
                }
            }
        } else if (nameEq(kp, klen, "Seasons")) {
            js.need('{');
            while (true) {
                js.ws();
                if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                size_t nl; const char* np = js.str(nl);
                js.need(':');
                int sid = seaId(np, nl);
                js.need('[');
                vector<int> per;
                while (true) {
                    js.ws();
                    if (js.p < js.e && *js.p == ']') { ++js.p; break; }
                    if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                    per.push_back((int)js.inum());
                }
                seaPerT[sid] = std::move(per);
            }
        } else if (nameEq(kp, klen, "Interventions")) {
            js.need('{');
            while (true) {
                js.ws();
                if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                size_t nl; const char* np = js.str(nl);
                js.need(':');
                intNameTmp.push_back(string(np, nl));
                int iTmax = -1;
                tDeltaBuf.clear();
                tmpWl.clear();
                tmpRk.clear();
                js.need('{');
                while (true) {
                    js.ws();
                    if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                    if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                    size_t fl; const char* fp = js.str(fl);
                    js.need(':');
                    if (nameEq(fp, fl, "tmax")) {
                        iTmax = (int)js.inum();
                    } else if (nameEq(fp, fl, "Delta")) {
                        js.need('[');
                        while (true) {
                            js.ws();
                            if (js.p < js.e && *js.p == ']') { ++js.p; break; }
                            if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                            i64 v = js.inum();
                            if (v < 0) v = 0;
                            tDeltaBuf.push_back((int)v);
                        }
                    } else if (nameEq(fp, fl, "workload")) {
                        js.need('{');
                        while (true) {
                            js.ws();
                            if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                            if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                            size_t rl; const char* rp = js.str(rl);
                            int rid = resId(rp, rl);
                            js.need(':');
                            js.need('{');
                            while (true) {
                                js.ws();
                                if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                                size_t tl; const char* tp = js.str(tl);
                                int tt = keyInt(tp, tl);
                                js.need(':');
                                js.need('{');
                                while (true) {
                                    js.ws();
                                    if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                                    if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                                    size_t sl; const char* sp = js.str(sl);
                                    int ss = keyInt(sp, sl);
                                    js.need(':');
                                    double v = js.num();
                                    if (v != 0.0) tmpWl.push_back(TWl{ss, rid, tt, v});
                                }
                            }
                        }
                    } else if (nameEq(fp, fl, "risk")) {
                        js.need('{');
                        while (true) {
                            js.ws();
                            if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                            if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                            size_t tl; const char* tp = js.str(tl);
                            int tt = keyInt(tp, tl);
                            js.need(':');
                            js.need('{');
                            while (true) {
                                js.ws();
                                if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                                size_t sl; const char* sp = js.str(sl);
                                int ss = keyInt(sp, sl);
                                js.need(':');
                                js.need('[');
                                i64 off = (i64)rkVal.size();
                                double sum = 0.0;
                                while (true) {
                                    js.ws();
                                    if (js.p < js.e && *js.p == ']') { ++js.p; break; }
                                    if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                                    double v = js.num();
                                    rkVal.push_back(v);
                                    sum += v;
                                }
                                tmpRk.push_back(TRk{ss, tt, off, sum});
                            }
                        }
                    } else {
                        js.skipValue();
                    }
                }
                // ---- layout for this intervention
                if (iTmax < 0) iTmax = (int)tDeltaBuf.size();
                int tmx = iTmax;
                if (tmx > (int)tDeltaBuf.size()) tmx = (int)tDeltaBuf.size();
                if (tmx < 0) tmx = 0;
                tmaxTmp.push_back(tmx);
                int base = (int)delta.size();
                slotBase.push_back(base);
                for (int st = 1; st <= tmx; st++) delta.push_back(tDeltaBuf[st - 1]);
                stCnt.assign(tmx + 2, 0);
                for (auto& w : tmpWl) if (w.st >= 1 && w.st <= tmx) stCnt[w.st]++;
                i64 wlBase = (i64)wlVal.size();
                wlVal.resize(wlBase + tmpWl.size());
                wlRes.resize(wlBase + tmpWl.size());
                wlT.resize(wlBase + tmpWl.size());
                wlStart.resize((size_t)base + tmx + 1, 0);
                i64 wlRun = wlBase;
                for (int st = 1; st <= tmx; st++) { wlStart[base + st - 1] = wlRun; wlRun += stCnt[st]; }
                {
                    vector<i64> cur(base + tmx + 1);
                    for (int st = 1; st <= tmx; st++) cur[base + st - 1] = wlStart[base + st - 1];
                    for (auto& w : tmpWl) {
                        if (w.st < 1 || w.st > tmx) continue;
                        i64 pos = cur[base + w.st - 1]++;
                        wlVal[pos] = w.v; wlRes[pos] = w.res; wlT[pos] = w.t;
                    }
                }
                wlStart[base + tmx] = wlRun;
                stCnt.assign(tmx + 2, 0);
                for (auto& r : tmpRk) if (r.st >= 1 && r.st <= tmx) stCnt[r.st]++;
                i64 rkBase = (i64)rkRecT.size();
                rkRecT.resize(rkBase + tmpRk.size());
                rkRecOff.resize(rkBase + tmpRk.size());
                rkRecSum.resize(rkBase + tmpRk.size());
                rkStart.resize((size_t)base + tmx + 1, 0);
                i64 rkRun = rkBase;
                for (int st = 1; st <= tmx; st++) { rkStart[base + st - 1] = rkRun; rkRun += stCnt[st]; }
                {
                    vector<i64> cur(base + tmx + 1);
                    for (int st = 1; st <= tmx; st++) cur[base + st - 1] = rkStart[base + st - 1];
                    for (auto& r : tmpRk) {
                        if (r.st < 1 || r.st > tmx) continue;
                        i64 pos = cur[base + r.st - 1]++;
                        rkRecT[pos] = r.t; rkRecOff[pos] = r.off; rkRecSum[pos] = r.sum;
                    }
                }
                rkStart[base + tmx] = rkRun;
            }
        } else if (nameEq(kp, klen, "Exclusions")) {
            js.need('{');
            while (true) {
                js.ws();
                if (js.p < js.e && *js.p == '}') { ++js.p; break; }
                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                size_t el; js.str(el);
                js.need(':');
                js.need('[');
                int k = 0;
                string i1, i2; int sd = -1;
                while (true) {
                    js.ws();
                    if (js.p < js.e && *js.p == ']') { ++js.p; break; }
                    if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                    size_t sl; const char* sp = js.str(sl);
                    if (k == 0) i1.assign(sp, sl);
                    else if (k == 1) i2.assign(sp, sl);
                    else sd = seaId(sp, sl);
                    k++;
                }
                ex1.push_back(i1); ex2.push_back(i2); exSea.push_back(sd);
            }
        } else if (nameEq(kp, klen, "T")) {
            T = (int)js.inum();
        } else if (nameEq(kp, klen, "Scenarios_number")) {
            js.need('[');
            Sbuf.clear();
            while (true) {
                js.ws();
                if (js.p < js.e && *js.p == ']') { ++js.p; break; }
                if (js.p < js.e && *js.p == ',') { ++js.p; continue; }
                Sbuf.push_back(js.num());
            }
        } else if (nameEq(kp, klen, "Quantile")) {
            quant = js.num();
        } else if (nameEq(kp, klen, "Alpha")) {
            alpha = js.num();
        } else {
            js.skipValue();
        }
    }
    resName = std::move(resNameTmp);
    nRes = (int)resName.size();
    seaName = std::move(seaNameTmp);
    nSea = (int)seaName.size();
    intName = std::move(intNameTmp);
    nInt = (int)intName.size();
    tmax = std::move(tmaxTmp);
    slotBase.push_back((int)delta.size());
    S.resize(Sbuf.size());
    for (size_t i = 0; i < Sbuf.size(); i++) S[i] = (int)llround(Sbuf[i]);
    if (T <= 0) T = (int)S.size();
    maxS = 1;
    sumS = 0;
    for (int v : S) { maxS = max(maxS, v); sumS += v; }
    rmin.assign((size_t)nRes * T, 0.0);
    rmax.assign((size_t)nRes * T, 1e300);
    for (int r = 0; r < nRes; r++) {
        for (int t = 0; t < T; t++) {
            if (t < (int)resMinT[r].size()) rmin[(size_t)r * T + t] = resMinT[r][t];
            if (t < (int)resMaxT[r].size()) rmax[(size_t)r * T + t] = resMaxT[r][t];
        }
    }
    seaPref.assign((size_t)max(nSea, 1) * (T + 2), 0);
    for (int s = 0; s < nSea; s++) {
        int* pf = &seaPref[(size_t)s * (T + 2)];
        for (int t : seaPerT[s]) if (t >= 1 && t <= T) pf[t]++;
        for (int t = 1; t <= T; t++) pf[t] += pf[t - 1];
    }
    {
        unordered_map<string, int> idOf;
        idOf.reserve((size_t)nInt * 2 + 16);
        for (int i = 0; i < nInt; i++) idOf[intName[i]] = i;
        excA.clear(); excB.clear(); excSea.clear();
        excA.reserve(ex1.size()); excB.reserve(ex1.size()); excSea.reserve(ex1.size());
        for (size_t k = 0; k < ex1.size(); k++) {
            auto it1 = idOf.find(ex1[k]);
            auto it2 = idOf.find(ex2[k]);
            if (it1 == idOf.end() || it2 == idOf.end()) continue;
            excA.push_back(it1->second);
            excB.push_back(it2->second);
            excSea.push_back(exSea[k] < 0 ? 0 : exSea[k]);
        }
    }
    if (verbose) {
        fprintf(stderr, "[parse] T=%d nRes=%d nInt=%d nExc=%d slots=%zu wlRec=%zu rkRec=%zu rkVal=%zu time=%.2fs mem=%ldMB\n",
                T, nRes, nInt, (int)excA.size(), (size_t)rkStart.size() - 1,
                (size_t)wlVal.size(), (size_t)rkRecT.size(), (size_t)rkVal.size(), nowSec() - t0,
                (long)((i64)rkVal.size() * 8 / 1048576));
    }
    return true;
}

void Instance::finalize(bool verbose) {
    double t0 = nowSec();
    int slots = (int)delta.size();
    okStart.assign(slots, 0);
    slotHi.assign(nInt, 0);
    for (int i = 0; i < nInt; i++) {
        for (int st = 1; st <= tmax[i]; st++) {
            int sl = slot(i, st);
            int d = delta[sl];
            bool ok = (st >= 1) && (st <= T) && (d >= 1) && (st + d - 1 <= T);
            if (ok) {
                // every period of the window must have a risk entry for this start
                int found = 0;
                for (i64 rr = rkStart[sl]; rr < rkStart[sl + 1]; rr++) {
                    int tt = rkRecT[rr];
                    if (tt >= st && tt < st + d) found++;
                }
                if (found != d) ok = false;
            }
            okStart[sl] = ok ? 1 : 0;
            if (ok) slotHi[i] = st;
        }
    }
    cArr.assign(slots, 0.0);
    for (int i = 0; i < nInt; i++) {
        for (int st = 1; st <= tmax[i]; st++) {
            int sl = slot(i, st);
            if (!okStart[sl]) continue;
            int d = delta[sl];
            double acc = 0.0;
            for (i64 rr = rkStart[sl]; rr < rkStart[sl + 1]; rr++) {
                int tt = rkRecT[rr];
                if (tt < 1 || tt > T) continue;
                if (tt < st || tt >= st + d) continue;   // outside the window
                acc += rkRecSum[rr] / (double)S[tt - 1];
            }
            cArr[sl] = acc / (double)T;
        }
    }
    adjStart.assign(nInt + 1, 0);
    for (size_t k = 0; k < excA.size(); k++) { adjStart[excA[k] + 1]++; adjStart[excB[k] + 1]++; }
    for (int i = 0; i < nInt; i++) adjStart[i + 1] += adjStart[i];
    adjJ.assign(adjStart[nInt], 0);
    adjSea.assign(adjStart[nInt], 0);
    {
        vector<i64> cur(adjStart.begin(), adjStart.end());
        for (size_t k = 0; k < excA.size(); k++) {
            int a = excA[k], b = excB[k], s = excSea[k];
            adjJ[cur[a]] = b; adjSea[cur[a]] = s; cur[a]++;
            adjJ[cur[b]] = a; adjSea[cur[b]] = s; cur[b]++;
        }
    }
    totalWlRec = (i64)wlVal.size();
    totalRkRec = (i64)rkRecT.size();
    totalRkVal = (i64)rkVal.size();
    if (verbose) fprintf(stderr, "[finalize] %.2fs\n", nowSec() - t0);
}

// ------------------------------------------------------------------ RNG
struct RNG {
    uint64_t s;
    explicit RNG(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    inline uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    inline uint32_t u32() { return (uint32_t)(next() >> 32); }
    inline int i01(int n) { return (int)(u32() % (uint32_t)n); }         // [0,n)
    inline double u01() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
};

// ------------------------------------------------------------------ statistics helpers
static double cpuSeconds() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + 1e-6 * (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec);
}
static long peakRssKB() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

