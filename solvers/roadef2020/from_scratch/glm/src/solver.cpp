// ROADEF2020 RTE maintenance planning solver — designed and implemented from scratch.
// Only the C++ standard library is used; no external solver or parser libraries.
//
// Build:  g++ -O2 -std=c++17 -pthread -o solver solver.cpp
// Usage:  ./solver --instance X.json --solution out.txt [--seed 1] [--time-budget 870]
//                     [--threads 4] [--samples 96] [--stats] [--summary s.json]
//
// Problem (per official subject + official checker):
//   pick start[i] in [1..tmax[i]] for every intervention; i started at s is active
//   during [s, s+delta(i,s)-1], consuming workload and adding per-scenario risk.
//   Constraints: per-(resource,t) usage in [min,max] (tol 1e-5); exclusion pairs
//   never simultaneously active during their season.
//   Objective = alpha*mean_t(meanRisk_t) + (1-alpha)*mean_t(max(0, Qtau_t - meanRisk_t)),
//   Qtau_t = ceil(tau*S_t)-th smallest summed risk at t (checker convention).
//
// Method: ALNS with SA acceptance, regret-2 greedy repair, several destroy
// operators, penalty-guided feasibility, scenario sub-sampling for the quantile
// term during search (full-data exact evaluation of final bests), and
// independent replicas (one per logical CPU) sharing a best-solution pool.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cerrno>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <limits>
#include <charconv>
#include <sys/resource.h>

static double now_sec() {
    using clk = std::chrono::steady_clock;
    return std::chrono::duration<double>(clk::now().time_since_epoch()).count();
}

static uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
struct RNG {
    uint64_t s;
    explicit RNG(uint64_t seed) : s(seed ? seed : 0x123456789ABCDEFULL) {}
    uint64_t next() { s = splitmix64(s); return s; }
    uint32_t u32() { return (uint32_t)(next() >> 32); }
    double unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
    int below(int n) { return (int)(u32() % (uint32_t)n); }
    double uniform(double a, double b) { return a + (b - a) * unit(); }
};

// ------------------------------------------------------------------ JSON reader

struct JsonError { std::string msg; explicit JsonError(std::string m) : msg(std::move(m)) {} };

struct Reader {
    FILE* f = nullptr;
    std::vector<char> buf;
    size_t pos = 0, len = 0;
    bool eof = false;
    uint64_t bytesRead = 0;
    explicit Reader(size_t capBytes = 1u << 22) : buf(capBytes) {}
    ~Reader() { if (f) fclose(f); }
    void open(const std::string& path) {
        f = fopen(path.c_str(), "rb");
        if (!f) throw JsonError("cannot open file: " + path);
        setvbuf(f, nullptr, _IONBF, 0);
    }
    bool refill(size_t need) {
        if (len - pos >= need) return true;
        if (pos > 0) { memmove(buf.data(), buf.data() + pos, len - pos); len -= pos; pos = 0; }
        while (len < need) {
            if (eof) return len >= need;
            size_t got = fread(buf.data() + len, 1, buf.size() - len, f);
            if (got == 0) { eof = true; return len >= need; }
            len += got;
            bytesRead += got;
        }
        return true;
    }
    inline int peek() {
        if (pos >= len && !refill(1)) return -1;
        return (unsigned char)buf[pos];
    }
    inline void adv() { ++pos; }
    bool skipWs() {
        for (;;) {
            int c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') adv();
            else return c != -1;
        }
    }
    void expect(char c) {
        if (!skipWs()) throw JsonError(std::string("EOF, expected '") + c + "'");
        int p = peek();
        if (p != c) throw JsonError(std::string("expected '") + c + "' got '" + (char)p + "'");
        adv();
    }
    void parseStr(std::string& out) {
        expect('"');
        out.clear();
        for (;;) {
            if (pos >= len && !refill(64) && pos >= len) throw JsonError("EOF inside string");
            char c = buf[pos++];
            if (c == '"') return;
            if (c == '\\') {
                if (pos >= len && !refill(8)) throw JsonError("EOF in escape");
                char e = buf[pos++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > len && !refill(8)) throw JsonError("EOF in \\u");
                        unsigned v = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = buf[pos++]; v <<= 4;
                            if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                            else throw JsonError("bad \\u escape");
                        }
                        if (v < 0x80) out.push_back((char)v);
                        else if (v < 0x800) { out.push_back((char)(0xC0 | (v >> 6))); out.push_back((char)(0x80 | (v & 63))); }
                        else { out.push_back((char)(0xE0 | (v >> 12))); out.push_back((char)(0x80 | ((v >> 6) & 63))); out.push_back((char)(0x80 | (v & 63))); }
                        break;
                    }
                    default: throw JsonError("bad escape in string");
                }
                continue;
            }
            out.push_back(c);
        }
    }
    double parseNum() {
        if (!skipWs()) throw JsonError("EOF expecting number");
        refill(96);
        const char* first = buf.data() + pos;
        const char* last = buf.data() + len;
        double val = 0;
        auto res = std::from_chars(first, last, val);
        if (res.ec != std::errc() || res.ptr == first) {
            char tmp[160]; size_t n = 0;
            while (n < 159) {
                int c = peek();
                if (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' || (c >= '0' && c <= '9')) { tmp[n++] = (char)c; adv(); }
                else break;
            }
            tmp[n] = 0;
            if (n == 0) {
                char ctx[48]; size_t m = 0;
                for (size_t z = pos; z < len && m < 47; ++z) ctx[m++] = buf[z];
                ctx[m] = 0;
                throw JsonError(std::string("malformed number at byte ") +
                                std::to_string(bytesRead - (len - pos)) + " ctx: [" + ctx + "]");
            }
            return strtod(tmp, nullptr);
        }
        pos += (size_t)(res.ptr - first);
        return val;
    }
    uint64_t byteOffset() const { return bytesRead - (len - pos); }
    long long parseInt() { return (long long)llround(parseNum()); }
    // tolerant readers: accept plain JSON numbers or numbers written as strings ("38")
    double numAny() {
        if (skipWs() && peek() == '"') { std::string sv; parseStr(sv); return strtod(sv.c_str(), nullptr); }
        return parseNum();
    }
    long long intAny() { return (long long)llround(numAny()); }
    void skipLit(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (peek() != *p) throw JsonError(std::string("bad literal, expected ") + lit);
            adv();
        }
    }
    void skipValue() {
        if (!skipWs()) throw JsonError("EOF in skipValue");
        int c = peek();
        if (c == '{') {
            adv();
            if (skipWs() && peek() == '}') { adv(); return; }
            for (;;) {
                std::string key; parseStr(key);
                expect(':');
                skipValue();
                if (!skipWs()) throw JsonError("EOF in object");
                if (peek() == ',') { adv(); continue; }
                if (peek() == '}') { adv(); return; }
                throw JsonError("bad object syntax");
            }
        } else if (c == '[') {
            adv();
            if (skipWs() && peek() == ']') { adv(); return; }
            for (;;) {
                skipValue();
                if (!skipWs()) throw JsonError("EOF in array");
                if (peek() == ',') { adv(); continue; }
                if (peek() == ']') { adv(); return; }
                throw JsonError("bad array syntax");
            }
        } else if (c == '"') {
            std::string s; parseStr(s);
        } else if (c == 't') skipLit("true");
        else if (c == 'f') skipLit("false");
        else if (c == 'n') skipLit("null");
        else {
            refill(96); // ensure a full number is visible across buffer boundaries
            const char* first = buf.data() + pos;
            const char* last = buf.data() + len;
            double val = 0;
            auto res = std::from_chars(first, last, val);
            if (res.ec != std::errc() || res.ptr == first) throw JsonError("malformed number in skip");
            pos += (size_t)(res.ptr - first);
        }
    }
    // walk a risk object counting (t,st) entries and numbers, without storing values
    void countRiskNumbers(uint64_t& entries, uint64_t& numbers) {
        expect('{');
        entries = 0; numbers = 0;
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string tk; parseStr(tk);
            expect(':');
            expect('{');
            if (!(skipWs() && peek() == '}')) {
                for (;;) {
                    std::string sk; parseStr(sk);
                    expect(':');
                    expect('[');
                    if (!(skipWs() && peek() == ']')) {
                        for (;;) {
                            if (!skipWs()) throw JsonError("EOF in number");
                            int c = peek();
                            bool any = false;
                            while (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' || (c >= '0' && c <= '9')) { any = true; adv(); c = peek(); }
                            if (!any) throw JsonError("malformed number in risk array");
                            ++numbers;
                            if (!skipWs()) throw JsonError("EOF in risk array");
                            if (peek() == ',') { adv(); continue; }
                            if (peek() == ']') { adv(); break; }
                            throw JsonError("bad risk array syntax");
                        }
                    } else adv();
                    ++entries;
                    if (!skipWs()) throw JsonError("EOF in risk st object");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == '}') { adv(); break; }
                    throw JsonError("bad risk st object syntax");
                }
            } else adv();
            if (!skipWs()) throw JsonError("EOF in risk t object");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad risk t object syntax");
        }
    }
};

// ------------------------------------------------------------------ instance

struct Excl { int i, j, season; };

struct Instance {
    int T = 0, N = 0, R = 0;
    double alpha = 0.5, tau = 0.95;
    int computationTime = 0;
    std::vector<int> S;
    std::vector<std::vector<double>> resMin, resMax;
    std::vector<std::string> resName;
    std::unordered_map<std::string, int> resIdx;
    std::vector<std::string> iName;
    std::unordered_map<std::string, int> iIdx;
    std::vector<int> tmax;
    std::vector<std::vector<int>> delta;

    // workload CSR per (i,s)
    std::vector<std::vector<uint32_t>> wlOff;
    std::vector<uint32_t> wlRes, wlT;
    std::vector<float> wlW;
    // mean-risk CSR per (i,s)
    std::vector<std::vector<uint32_t>> mrOff;
    std::vector<uint32_t> mrT;
    std::vector<float> mrV;
    // sampled-risk CSR per (i,s)
    std::vector<std::vector<uint32_t>> srOff;
    std::vector<uint32_t> srT, srBase;
    std::vector<float> srV;
    // full-risk CSR per (i,s) (optional, for exact evaluation)
    bool storeFull = false;
    std::vector<std::vector<uint32_t>> frOff;
    std::vector<uint32_t> frT, frBase;
    std::vector<float> frV;

    int K = 0;
    std::vector<int> kOf;
    std::vector<std::vector<uint16_t>> sampleIdx;

    std::vector<std::vector<float>> meanSum; // N x tmax

    std::vector<Excl> excls;
    std::vector<std::vector<uint32_t>> adjEx;
    std::vector<std::vector<uint64_t>> seasonMask;
    std::vector<std::string> seasonName;
    std::unordered_map<std::string, int> seasonIdx;
    std::vector<std::array<std::string, 3>> exclRaw;

    uint64_t riskEntries = 0, riskNumbers = 0, wlEntries = 0, mrEntries = 0;
    // static potential users per (r,t): intervention ids with any workload entry there
    std::vector<std::vector<uint32_t>> rtUsers; // [r][t] -> unique intervention ids
};

static inline bool seasonHits(const std::vector<uint64_t>& mask, int a, int b) {
    if (b <= a) return false;
    int w1 = (b - 1) >> 6;
    for (int w = a >> 6; w <= w1; ++w) {
        int lo = std::max(a, w << 6), hi = std::min(b, (w + 1) << 6);
        uint64_t rm = (hi - lo >= 64) ? ~0ULL : (((1ULL << (hi - lo)) - 1) << (lo & 63));
        if (mask[w] & rm) return true;
    }
    return false;
}

// ------------------------------------------------------------------ parser

struct Parser {
    Instance& I;
    int pass;
    Reader rd;

    struct WEnt { int s, t, r; float w; };
    std::vector<WEnt> wbuf;
    std::vector<int> rS, rT_;
    std::vector<uint32_t> rOff;
    std::vector<float> rVals;
    std::vector<std::pair<int, std::vector<int>>> seasonTimes;

    explicit Parser(Instance& inst, int passNum) : I(inst), pass(passNum) {}

    // forwards to the reader so parsing helpers can be called unqualified
    inline bool skipWs() { return rd.skipWs(); }
    inline int peek() { return rd.peek(); }
    inline void adv() { rd.adv(); }
    void expect(char c) { rd.expect(c); }
    void parseStr(std::string& o) { rd.parseStr(o); }
    double parseNum() { return rd.parseNum(); }
    long long parseInt() { return rd.parseInt(); }
    void skipValue() { rd.skipValue(); }

    void flushWorkload(int ii) {
        auto& off = I.wlOff[ii];
        off.assign(I.tmax[ii] + 1, 0);
        std::vector<std::vector<WEnt>> buckets(I.tmax[ii]);
        for (auto& e : wbuf) buckets[e.s].push_back(e);
        size_t tot = 0;
        for (auto& b : buckets) tot += b.size();
        I.wlRes.reserve(I.wlRes.size() + tot);
        I.wlT.reserve(I.wlT.size() + tot);
        I.wlW.reserve(I.wlW.size() + tot);
        for (int s = 0; s < I.tmax[ii]; ++s) {
            off[s] = (uint32_t)I.wlW.size();
            for (auto& e : buckets[s]) {
                I.wlRes.push_back((uint32_t)e.r);
                I.wlT.push_back((uint32_t)e.t);
                I.wlW.push_back(e.w);
            }
        }
        off[I.tmax[ii]] = (uint32_t)I.wlW.size();
        I.wlEntries += tot;
        wbuf.clear();
    }

    void flushRisk(int ii) {
        const int tmi = I.tmax[ii];
        auto& mroff = I.mrOff[ii]; mroff.assign(tmi + 1, 0);
        auto& sroff = I.srOff[ii]; sroff.assign(tmi + 1, 0);
        auto& froff = I.frOff[ii]; if (I.storeFull) froff.assign(tmi + 1, 0);
        std::vector<std::vector<uint32_t>> buckets(tmi);
        for (uint32_t idx = 0; idx < rS.size(); ++idx) {
            int s = rS[idx], t = rT_[idx];
            if (s < 0 || s >= tmi) continue;
            int d = I.delta[ii][s];
            if (t < s || t >= s + d || t >= I.T) continue;
            buckets[s].push_back(idx);
        }
        I.meanSum[ii].assign(tmi, 0.f);
        for (int s = 0; s < tmi; ++s) {
            mroff[s] = (uint32_t)I.mrT.size();
            sroff[s] = (uint32_t)I.srT.size();
            if (I.storeFull) froff[s] = (uint32_t)I.frT.size();
            double ms = 0;
            for (uint32_t idx : buckets[s]) {
                int t = rT_[idx];
                uint32_t base = rOff[idx];
                int S = I.S[t];
                const float* v = rVals.data() + base;
                double sum = 0;
                for (int k = 0; k < S; ++k) sum += v[k];
                float mean = (float)(sum / S);
                I.mrT.push_back((uint32_t)t);
                I.mrV.push_back(mean);
                ms += mean;
                int kk = I.kOf[t];
                uint32_t sb = (uint32_t)I.srV.size();
                for (int q = 0; q < kk; ++q) I.srV.push_back(v[I.sampleIdx[t][q]]);
                I.srT.push_back((uint32_t)t);
                I.srBase.push_back(sb);
                if (I.storeFull) {
                    uint32_t fb = (uint32_t)I.frV.size();
                    for (int k = 0; k < S; ++k) I.frV.push_back(v[k]);
                    I.frT.push_back((uint32_t)t);
                    I.frBase.push_back(fb);
                }
            }
            I.meanSum[ii][s] = (float)ms;
        }
        mroff[tmi] = (uint32_t)I.mrT.size();
        sroff[tmi] = (uint32_t)I.srT.size();
        if (I.storeFull) froff[tmi] = (uint32_t)I.frT.size();
        size_t before = (size_t)mroff[0];
        I.mrEntries += (uint64_t)I.mrT.size() - before;
        rS.clear(); rT_.clear(); rOff.clear(); rVals.clear();
    }

    void parseInterventions() {
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string name; parseStr(name);
            expect(':');
            int ii;
            if (pass == 1) {
                ii = (int)I.iName.size();
                I.iName.push_back(name);
                I.iIdx[name] = ii;
                I.tmax.push_back(-1);
                I.delta.emplace_back();
                I.wlOff.emplace_back();
                I.mrOff.emplace_back();
                I.srOff.emplace_back();
                I.frOff.emplace_back();
                I.meanSum.emplace_back();
            } else {
                auto it = I.iIdx.find(name);
                if (it == I.iIdx.end()) throw JsonError("pass2: unknown intervention " + name);
                ii = it->second;
            }
            long long tmaxBuf = -1;
            std::vector<int> deltaBuf;
            expect('{');
            if (skipWs() && peek() == '}') { adv(); }
            else {
                for (;;) {
                    std::string key; parseStr(key);
                    expect(':');
                    if (key == "tmax") {
                        tmaxBuf = rd.intAny();
                    } else if (key == "Delta") {
                        expect('[');
                        if (!(skipWs() && peek() == ']')) {
                            for (;;) {
                                deltaBuf.push_back((int)rd.intAny());
                                if (!skipWs()) throw JsonError("EOF in Delta");
                                if (peek() == ',') { adv(); continue; }
                                if (peek() == ']') { adv(); break; }
                                throw JsonError("bad Delta syntax");
                            }
                        } else adv();
                    } else if (key == "workload") {
                        if (pass == 1) parseWorkload();
                        else rd.skipValue();
                    } else if (key == "risk") {
                        if (pass == 1) {
                            uint64_t e = 0, n = 0;
                            rd.countRiskNumbers(e, n);
                            I.riskEntries += e; I.riskNumbers += n;
                        } else {
                            parseRisk();
                        }
                    } else {
                        rd.skipValue();
                    }
                    if (!skipWs()) throw JsonError("EOF in intervention");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == '}') { adv(); break; }
                    throw JsonError("bad intervention object syntax");
                }
            }
            if (pass == 1) {
                if (tmaxBuf < 1) throw JsonError("intervention " + name + " missing/invalid tmax");
                I.tmax[ii] = (int)tmaxBuf;
                if ((int)deltaBuf.size() < I.tmax[ii])
                    throw JsonError("intervention " + name + " Delta shorter than tmax");
                I.delta[ii].assign(deltaBuf.begin(), deltaBuf.begin() + I.tmax[ii]);
                flushWorkload(ii);
            } else {
                flushRisk(ii);
            }
            if (!skipWs()) throw JsonError("EOF in Interventions");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad Interventions object syntax");
        }
    }

    void parseWorkload() {
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string resName; parseStr(resName);
            expect(':');
            auto rit = I.resIdx.find(resName);
            if (rit == I.resIdx.end()) throw JsonError("workload references unknown resource " + resName);
            int r = rit->second;
            expect('{');
            if (!(skipWs() && peek() == '}')) {
                for (;;) {
                    std::string tk; parseStr(tk);
                    expect(':');
                    int t0 = atoi(tk.c_str()) - 1;
                    expect('{');
                    if (!(skipWs() && peek() == '}')) {
                        for (;;) {
                            std::string sk; parseStr(sk);
                            expect(':');
                            double w = rd.numAny();
                            WEnt e; e.s = atoi(sk.c_str()) - 1; e.t = t0; e.r = r; e.w = (float)w;
                            wbuf.push_back(e);
                            if (!skipWs()) throw JsonError("EOF in workload st");
                            if (peek() == ',') { adv(); continue; }
                            if (peek() == '}') { adv(); break; }
                            throw JsonError("bad workload st object");
                        }
                    } else adv();
                    if (!skipWs()) throw JsonError("EOF in workload t");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == '}') { adv(); break; }
                    throw JsonError("bad workload t object");
                }
            } else adv();
            if (!skipWs()) throw JsonError("EOF in workload resource");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad workload object");
        }
    }

    void parseRisk() {
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string tk; parseStr(tk);
            expect(':');
            int t0 = atoi(tk.c_str()) - 1;
            if (t0 < 0 || t0 >= I.T) throw JsonError("risk time out of range: " + tk);
            int S = I.S[t0];
            expect('{');
            if (!(skipWs() && peek() == '}')) {
                for (;;) {
                    std::string sk; parseStr(sk);
                    expect(':');
                    int s0 = atoi(sk.c_str()) - 1;
                    expect('[');
                    uint32_t base = (uint32_t)rVals.size();
                    if (!(skipWs() && peek() == ']')) {
                        for (;;) {
                            rVals.push_back((float)rd.numAny());
                            if (!skipWs()) throw JsonError("EOF in risk array");
                            if (peek() == ',') { adv(); continue; }
                            if (peek() == ']') { adv(); break; }
                            throw JsonError("bad risk array");
                        }
                    } else adv();
                    if ((int)(rVals.size() - base) != S)
                        throw JsonError("risk array length != Scenarios_number at t=" + tk);
                    rS.push_back(s0); rT_.push_back(t0); rOff.push_back(base);
                    if (!skipWs()) throw JsonError("EOF in risk st");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == '}') { adv(); break; }
                    throw JsonError("bad risk st object");
                }
            } else adv();
            if (!skipWs()) throw JsonError("EOF in risk t");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad risk object");
        }
    }

    void run(const std::string& path) {
        try {
            runInner(path);
        } catch (JsonError& e) {
            throw JsonError(e.msg + " (at byte " + std::to_string(rd.byteOffset()) + ")");
        }
    }
    void runInner(const std::string& path) {
        rd.open(path);
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string key; parseStr(key);
            expect(':');
            if (key == "Resources") {
                if (pass == 1) parseResources(); else rd.skipValue();
            } else if (key == "Seasons") {
                if (pass == 1) parseSeasons(); else rd.skipValue();
            } else if (key == "Interventions") {
                parseInterventions();
            } else if (key == "Exclusions") {
                if (pass == 1) parseExclusions(); else rd.skipValue();
            } else if (key == "T") {
                if (pass == 1) I.T = (int)rd.intAny(); else rd.skipValue();
            } else if (key == "Scenarios_number") {
                if (pass == 1) {
                    I.S.clear();
                    expect('[');
                    if (!(skipWs() && peek() == ']')) {
                        for (;;) {
                            I.S.push_back((int)rd.intAny());
                            if (!skipWs()) throw JsonError("EOF in Scenarios_number");
                            if (peek() == ',') { adv(); continue; }
                            if (peek() == ']') { adv(); break; }
                            throw JsonError("bad Scenarios_number");
                        }
                    } else adv();
                } else rd.skipValue();
            } else if (key == "Quantile") {
                if (pass == 1) I.tau = rd.numAny(); else rd.skipValue();
            } else if (key == "Alpha") {
                if (pass == 1) I.alpha = rd.numAny(); else rd.skipValue();
            } else if (key == "ComputationTime") {
                if (pass == 1) I.computationTime = (int)rd.intAny(); else rd.skipValue();
            } else {
                rd.skipValue();
            }
            if (!skipWs()) throw JsonError("EOF in root");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad root object");
        }
    }

    void parseResources() {
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string name; parseStr(name);
            expect(':');
            int r;
            auto it = I.resIdx.find(name);
            if (it == I.resIdx.end()) {
                r = (int)I.resName.size();
                I.resName.push_back(name);
                I.resIdx[name] = r;
                I.resMin.emplace_back();
                I.resMax.emplace_back();
            } else r = it->second;
            std::vector<double> mn, mx;
            expect('{');
            if (!(skipWs() && peek() == '}')) {
                for (;;) {
                    std::string key; parseStr(key);
                    expect(':');
                    if (key == "min" || key == "max") {
                        std::vector<double>& dst = (key == "min") ? mn : mx;
                        dst.clear();
                        expect('[');
                        if (!(skipWs() && peek() == ']')) {
                            for (;;) {
                                dst.push_back(rd.numAny());
                                if (!skipWs()) throw JsonError("EOF in resource array");
                                if (peek() == ',') { adv(); continue; }
                                if (peek() == ']') { adv(); break; }
                                throw JsonError("bad resource array");
                            }
                        } else adv();
                    } else rd.skipValue();
                    if (!skipWs()) throw JsonError("EOF in resource");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == '}') { adv(); break; }
                    throw JsonError("bad resource object");
                }
            } else adv();
            I.resMin[r] = std::move(mn);
            I.resMax[r] = std::move(mx);
            if (!skipWs()) throw JsonError("EOF in Resources");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad Resources object");
        }
    }

    void parseSeasons() {
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string name; parseStr(name);
            expect(':');
            int id;
            auto it = I.seasonIdx.find(name);
            if (it == I.seasonIdx.end()) {
                id = (int)I.seasonName.size();
                I.seasonName.push_back(name);
                I.seasonIdx[name] = id;
            } else id = it->second;
            std::vector<int> times;
            expect('[');
            if (!(skipWs() && peek() == ']')) {
                for (;;) {
                    times.push_back((int)rd.intAny());
                    if (!skipWs()) throw JsonError("EOF in seasons");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == ']') { adv(); break; }
                    throw JsonError("bad seasons array");
                }
            } else adv();
            seasonTimes.push_back(std::make_pair(id, std::move(times)));
            if (!skipWs()) throw JsonError("EOF in Seasons");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad Seasons object");
        }
    }

    void parseExclusions() {
        expect('{');
        if (skipWs() && peek() == '}') { adv(); return; }
        for (;;) {
            std::string name; parseStr(name);
            expect(':');
            expect('[');
            std::array<std::string, 3> trio;
            int got = 0;
            if (!(skipWs() && peek() == ']')) {
                for (;;) {
                    std::string v; parseStr(v);
                    if (got < 3) trio[got] = v;
                    ++got;
                    if (!skipWs()) throw JsonError("EOF in exclusion");
                    if (peek() == ',') { adv(); continue; }
                    if (peek() == ']') { adv(); break; }
                    throw JsonError("bad exclusion array");
                }
            } else adv();
            if (got != 3) throw JsonError("exclusion entry with != 3 items");
            I.exclRaw.push_back(trio);
            if (!skipWs()) throw JsonError("EOF in Exclusions");
            if (peek() == ',') { adv(); continue; }
            if (peek() == '}') { adv(); return; }
            throw JsonError("bad Exclusions object");
        }
    }
};

static void finalizeInstance(Instance& I, RNG& rng) {
    if (I.T <= 0) throw JsonError("missing T");
    if ((int)I.S.size() != I.T) throw JsonError("Scenarios_number length != T");
    I.N = (int)I.iName.size();
    if (I.N == 0) throw JsonError("no interventions");
    I.R = (int)I.resName.size();
    for (auto& mn : I.resMin) if ((int)mn.size() != I.T) throw JsonError("resource min length != T");
    for (auto& mx : I.resMax) if ((int)mx.size() != I.T) throw JsonError("resource max length != T");
    for (int i = 0; i < I.N; ++i) {
        if (I.tmax[i] > I.T) { I.tmax[i] = I.T; I.delta[i].resize(I.tmax[i]); }
        if (I.tmax[i] < 1) throw JsonError("invalid tmax after clamp");
        for (int s = 0; s < I.tmax[i]; ++s)
            if (I.delta[i][s] < 1) throw JsonError("delta < 1");
    }
    I.adjEx.assign(I.N, {});
    for (auto& trio : I.exclRaw) {
        auto a = I.iIdx.find(trio[0]);
        auto b = I.iIdx.find(trio[1]);
        auto c = I.seasonIdx.find(trio[2]);
        if (a == I.iIdx.end() || b == I.iIdx.end()) throw JsonError("exclusion references unknown intervention");
        if (c == I.seasonIdx.end()) throw JsonError("exclusion references unknown season " + trio[2]);
        Excl e; e.i = a->second; e.j = b->second; e.season = c->second;
        if (e.i == e.j) continue;
        uint32_t id = (uint32_t)I.excls.size();
        I.excls.push_back(e);
        I.adjEx[e.i].push_back(id);
        I.adjEx[e.j].push_back(id);
    }
    I.kOf.resize(I.T);
    I.sampleIdx.resize(I.T);
    for (int t = 0; t < I.T; ++t) {
        int S = I.S[t];
        if (S < 1) throw JsonError("S_t < 1");
        int kk = std::min(S, I.K);
        I.kOf[t] = kk;
        I.sampleIdx[t].resize(kk);
        if (kk >= S) { for (int k = 0; k < kk; ++k) I.sampleIdx[t][k] = (uint16_t)k; }
        else {
            static thread_local std::vector<int> idx;
            idx.resize(S);
            for (int k = 0; k < S; ++k) idx[k] = k;
            for (int k = 0; k < kk; ++k) {
                int j = k + rng.below(S - k);
                std::swap(idx[k], idx[j]);
            }
            for (int k = 0; k < kk; ++k) I.sampleIdx[t][k] = (uint16_t)idx[k];
        }
    }
}

static void loadInstance(const std::string& path, Instance& I, uint64_t seed, int wantK) {
    RNG rngSeed(seed * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL);
    {
        Parser p1(I, 1);
        p1.run(path);
        size_t words = (I.T + 63) / 64;
        I.seasonMask.assign(I.seasonName.size(), std::vector<uint64_t>(words, 0));
        for (auto& pr : p1.seasonTimes)
            for (int t1 : pr.second)
                if (t1 >= 1 && t1 <= I.T)
                    I.seasonMask[pr.first][(t1 - 1) >> 6] |= 1ULL << ((t1 - 1) & 63);
    }
    {
        double memFull = (double)I.riskNumbers * 4.0;
        I.storeFull = (memFull <= 1.5e9);
        uint64_t ent = std::max<uint64_t>(1, I.riskEntries);
        int kByMem = (int)std::min(3.0e9 / ((double)ent * 4.0), 1e9);
        I.K = std::max(8, std::min(wantK, kByMem));
    }
    finalizeInstance(I, rngSeed);
    {
        Parser p2(I, 2);
        p2.run(path);
    }
    // build rtUsers (potential users of each (resource,time) cell)
    {
        std::vector<uint32_t> stamp((size_t)I.R * I.T, UINT32_MAX);
        std::vector<std::vector<uint32_t>> cells((size_t)I.R * I.T);
        for (int i = 0; i < I.N; ++i)
            for (int s = 0; s < I.tmax[i]; ++s)
                for (uint32_t k = I.wlOff[i][s]; k < I.wlOff[i][s + 1]; ++k) {
                    size_t cell = (size_t)I.wlRes[k] * I.T + I.wlT[k];
                    if (stamp[cell] != (uint32_t)i) { stamp[cell] = (uint32_t)i; cells[cell].push_back((uint32_t)i); }
                }
        I.rtUsers.assign((size_t)I.R * I.T, {});
        for (size_t c = 0; c < cells.size(); ++c) I.rtUsers[c] = std::move(cells[c]);
    }
    I.wlRes.shrink_to_fit(); I.wlT.shrink_to_fit(); I.wlW.shrink_to_fit();
    I.mrT.shrink_to_fit(); I.mrV.shrink_to_fit();
    I.srT.shrink_to_fit(); I.srBase.shrink_to_fit(); I.srV.shrink_to_fit();
    I.frT.shrink_to_fit(); I.frBase.shrink_to_fit(); I.frV.shrink_to_fit();
}

// ------------------------------------------------------------------ evaluation helpers

struct ExactEval { double obj1, obj2, obj; bool ok; };

static ExactEval evaluateExact(const Instance& I, const std::vector<int>& start) {
    ExactEval r{0, 0, 0, true};
    if (!I.storeFull) { r.ok = false; return r; }
    int Smax = *std::max_element(I.S.begin(), I.S.end());
    std::vector<double> meanRisk(I.T, 0.0);
    std::vector<float> sum((size_t)I.T * Smax);
    for (int i = 0; i < I.N; ++i) {
        int s = start[i];
        uint32_t b = I.frOff[i][s], e = I.frOff[i][s + 1];
        uint32_t mb = I.mrOff[i][s];
        for (uint32_t k = b; k < e; ++k) {
            int t = (int)I.frT[k];
            const float* v = I.frV.data() + I.frBase[k];
            float* dst = sum.data() + (size_t)t * Smax;
            int S = I.S[t];
            for (int q = 0; q < S; ++q) dst[q] += v[q];
            meanRisk[t] += I.mrV[mb + (k - b)];
        }
    }
    double o1 = 0, o2 = 0;
    std::vector<float> tmp;
    for (int t = 0; t < I.T; ++t) {
        int S = I.S[t];
        tmp.assign(sum.begin() + (size_t)t * Smax, sum.begin() + (size_t)t * Smax + S);
        std::sort(tmp.begin(), tmp.end());
        int qi = (int)std::ceil(I.tau * S) - 1;
        qi = std::max(0, std::min(S - 1, qi));
        double q = tmp[qi];
        double m = meanRisk[t];
        o1 += m;
        o2 += std::max(0.0, q - m);
    }
    r.obj1 = o1 / I.T;
    r.obj2 = o2 / I.T;
    r.obj = I.alpha * r.obj1 + (1 - I.alpha) * r.obj2;
    return r;
}

// sampled objective for an arbitrary start vector (used for final selection)
static double sampledObjectiveFor(const Instance& I, const std::vector<int>& start) {
    std::vector<double> meanRisk(I.T, 0.0);
    std::vector<float> srSum((size_t)I.T * I.K, 0.f);
    for (int i = 0; i < I.N; ++i) {
        int s = start[i];
        uint32_t b = I.mrOff[i][s], e = I.mrOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) meanRisk[I.mrT[k]] += I.mrV[k];
        b = I.srOff[i][s]; e = I.srOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) {
            int t = (int)I.srT[k];
            int kk = I.kOf[t];
            float* dst = srSum.data() + (size_t)t * I.K;
            const float* v = I.srV.data() + I.srBase[k];
            for (int z = 0; z < kk; ++z) dst[z] += v[z];
        }
    }
    double o1 = 0, o2 = 0;
    std::vector<float> tmp;
    for (int t = 0; t < I.T; ++t) {
        int kk = I.kOf[t];
        tmp.assign(srSum.begin() + (size_t)t * I.K, srSum.begin() + (size_t)t * I.K + kk);
        std::sort(tmp.begin(), tmp.end());
        int qi = (int)std::ceil(I.tau * kk) - 1;
        qi = std::max(0, std::min(kk - 1, qi));
        o1 += meanRisk[t];
        o2 += std::max(0.0, (double)tmp[qi] - meanRisk[t]);
    }
    return I.alpha * (o1 / I.T) + (1 - I.alpha) * (o2 / I.T);
}

struct Diag { double violRes; int violExcl; };
static Diag diagnose(const Instance& I, const std::vector<int>& start) {
    Diag d{0.0, 0};
    std::vector<double> use((size_t)I.R * I.T, 0.0);
    for (int i = 0; i < I.N; ++i) {
        int s = start[i];
        uint32_t b = I.wlOff[i][s], e = I.wlOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k)
            use[(size_t)I.wlRes[k] * I.T + I.wlT[k]] += I.wlW[k];
    }
    const double tol = 1e-5;
    for (int r = 0; r < I.R; ++r)
        for (int t = 0; t < I.T; ++t) {
            double u = use[(size_t)r * I.T + t];
            d.violRes += std::max(0.0, u - I.resMax[r][t] - tol) + std::max(0.0, I.resMin[r][t] - tol - u);
        }
    for (auto& ex : I.excls) {
        int s1 = start[ex.i], s2 = start[ex.j];
        int a = std::max(s1, s2);
        int b = std::min(s1 + I.delta[ex.i][s1], s2 + I.delta[ex.j][s2]);
        if (a < b && seasonHits(I.seasonMask[ex.season], a, b)) ++d.violExcl;
    }
    return d;
}

// ------------------------------------------------------------------ ALNS

struct Shared {
    std::mutex mu;
    std::vector<int> bestStart;
    double bestSampled = std::numeric_limits<double>::infinity();
    int improvedBy = -1;
    std::atomic<bool> stopFlag{false};
};

struct Searcher {
    const Instance& I;
    RNG rng;
    int id;
    double deadline;
    Shared& shared;
    std::vector<int> start;
    std::vector<double> resUse;
    std::vector<double> meanRisk;
    std::vector<float> srSum;
    std::vector<double> q;
    std::vector<float> qtmp;
    double violRes = 0;
    int violExcl = 0;
    double lamRes = 1.0, lamExcl = 1.0, scale = 1.0;
    std::vector<int> bestStartLocal;
    double bestLocalSampled = std::numeric_limits<double>::infinity();
    double lastImprove = 0;
    double T0 = 1, Temp = 1;
    static const int NOPS = 6;
    std::vector<double> opW;
    std::vector<uint64_t> opUsed, opScore;
    std::vector<int> removedList;
    uint64_t iterations = 0;

    Searcher(const Instance& inst, uint64_t seed, int id_, double deadline_, Shared& sh)
        : I(inst), rng(seed), id(id_), deadline(deadline_), shared(sh) {
        start.assign(I.N, -1);
        resUse.assign((size_t)I.R * I.T, 0.0);
        meanRisk.assign(I.T, 0.0);
        srSum.assign((size_t)I.T * I.K, 0.f);
        q.assign(I.T, 0.0);
        qtmp.assign(I.K + 2, 0.0f);
        opW.assign(NOPS, 1.0);
        repW.assign(NREP, 1.0);
        repUsed.assign(NREP, 0);
        repScore.assign(NREP, 0);
        opUsed.assign(NOPS, 0);
        opScore.assign(NOPS, 0);
    }

    static constexpr double VTOL = 1e-5; // checker tolerance on resource bounds
    inline double cellViol(int r, int t) const {
        double u = resUse[(size_t)r * I.T + t];
        return std::max(0.0, u - I.resMax[r][t] - VTOL) + std::max(0.0, I.resMin[r][t] - VTOL - u);
    }

    int conflictsOf(int i) const {
        int s = start[i];
        if (s < 0) return 0;
        int cnt = 0;
        for (uint32_t eid : I.adjEx[i]) {
            const Excl& ex = I.excls[eid];
            int o = ex.i == i ? ex.j : ex.i;
            int so = start[o];
            if (so < 0) continue;
            int a = std::max(s, so);
            int b = std::min(s + I.delta[i][s], so + I.delta[o][so]);
            if (a < b && seasonHits(I.seasonMask[ex.season], a, b)) ++cnt;
        }
        return cnt;
    }

    // touched time-step range (set by remove/place); refreshing q only inside
    // it is exact because srSum outside it did not change
    int touchLo = 1 << 30, touchHi = -1;
    inline void touch(int t) {
        if (t < touchLo) touchLo = t;
        if (t > touchHi) touchHi = t;
    }
    void markTouched(int i, int s) {
        for (uint32_t k = I.mrOff[i][s]; k < I.mrOff[i][s + 1]; ++k) touch((int)I.mrT[k]);
    }
    void resetTouch() { touchLo = 1 << 30; touchHi = -1; }
    void refreshTouchedQ() {
        if (touchHi < touchLo) return;
        for (int t = touchLo; t <= touchHi; ++t) q[t] = quantileOf(t);
        resetTouch();
    }

    void remove(int i) {
        int s = start[i];
        if (s < 0) return;
        markTouched(i, s);
        violExcl -= conflictsOf(i);
        uint32_t b = I.wlOff[i][s], e = I.wlOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) {
            int r = (int)I.wlRes[k], t = (int)I.wlT[k];
            double& u = resUse[(size_t)r * I.T + t];
            double before = std::max(0.0, u - I.resMax[r][t] - VTOL) + std::max(0.0, I.resMin[r][t] - VTOL - u);
            u -= I.wlW[k];
            double after = std::max(0.0, u - I.resMax[r][t] - VTOL) + std::max(0.0, I.resMin[r][t] - VTOL - u);
            violRes += after - before;
        }
        b = I.mrOff[i][s]; e = I.mrOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) meanRisk[I.mrT[k]] -= I.mrV[k];
        b = I.srOff[i][s]; e = I.srOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) {
            int t = (int)I.srT[k];
            int kk = I.kOf[t];
            float* dst = srSum.data() + (size_t)t * I.K;
            const float* v = I.srV.data() + I.srBase[k];
            for (int z = 0; z < kk; ++z) dst[z] -= v[z];
        }
        start[i] = -1;
    }

    void place(int i, int s) {
        markTouched(i, s);
        start[i] = s;
        uint32_t b = I.wlOff[i][s], e = I.wlOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) {
            int r = (int)I.wlRes[k], t = (int)I.wlT[k];
            double& u = resUse[(size_t)r * I.T + t];
            double before = std::max(0.0, u - I.resMax[r][t] - VTOL) + std::max(0.0, I.resMin[r][t] - VTOL - u);
            u += I.wlW[k];
            double after = std::max(0.0, u - I.resMax[r][t] - VTOL) + std::max(0.0, I.resMin[r][t] - VTOL - u);
            violRes += after - before;
        }
        b = I.mrOff[i][s]; e = I.mrOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) meanRisk[I.mrT[k]] += I.mrV[k];
        b = I.srOff[i][s]; e = I.srOff[i][s + 1];
        for (uint32_t k = b; k < e; ++k) {
            int t = (int)I.srT[k];
            int kk = I.kOf[t];
            float* dst = srSum.data() + (size_t)t * I.K;
            const float* v = I.srV.data() + I.srBase[k];
            for (int z = 0; z < kk; ++z) dst[z] += v[z];
        }
        violExcl += conflictsOf(i);
    }

    double quantileOf(int t) {
        int kk = I.kOf[t];
        const float* src = srSum.data() + (size_t)t * I.K;
        for (int z = 0; z < kk; ++z) qtmp[z] = src[z];
        int qi = (int)std::ceil(I.tau * kk) - 1;
        qi = std::max(0, std::min(kk - 1, qi));
        std::nth_element(qtmp.begin(), qtmp.begin() + qi, qtmp.begin() + kk);
        return qtmp[qi];
    }

    void refreshAllQ() { for (int t = 0; t < I.T; ++t) q[t] = quantileOf(t); }

    double sampledObjective() const {
        double o1 = 0, o2 = 0;
        for (int t = 0; t < I.T; ++t) {
            o1 += meanRisk[t];
            o2 += std::max(0.0, q[t] - meanRisk[t]);
        }
        o1 /= I.T; o2 /= I.T;
        return I.alpha * o1 + (1 - I.alpha) * o2;
    }

    // feasibility-focus mode: when no feasible solution has been accepted for a
    // long time, objective terms are dropped and the search minimizes violations
    bool feasOnly = false;
    double lastFeasibleT = 0, lastFeasSwitch = 0;
    std::vector<int> bestInfeasStart;
    double bestInfeasKey = 1e300;
    double totalCost() const {
        double base = feasOnly ? 0.0 : sampledObjective();
        return base + lamRes * violRes + lamExcl * violExcl;
    }

    double insertCost(int i, int s, double noise) {
        double c = 0;
        if (!feasOnly) {
            c += I.alpha * (double)I.meanSum[i][s] / I.T;
            uint32_t b = I.mrOff[i][s], e = I.mrOff[i][s + 1];
            double dsum = 0;
            for (uint32_t k = b; k < e; ++k) {
                int t = (int)I.mrT[k];
                double m = meanRisk[t], qq = q[t], v = I.mrV[k];
                dsum += std::max(0.0, qq - m - v) - std::max(0.0, qq - m);
            }
            c += (1 - I.alpha) * dsum / I.T;
        }
        {
            uint32_t b = I.wlOff[i][s], e = I.wlOff[i][s + 1];
            for (uint32_t k = b; k < e; ++k) {
                int r = (int)I.wlRes[k], t = (int)I.wlT[k];
                double u = resUse[(size_t)r * I.T + t] + I.wlW[k];
                c += lamRes * ((std::max(0.0, u - I.resMax[r][t] - VTOL) + std::max(0.0, I.resMin[r][t] - VTOL - u)) - cellViol(r, t));
            }
        }
        if (!I.adjEx[i].empty()) {
            int cf = 0;
            for (uint32_t eid : I.adjEx[i]) {
                const Excl& ex = I.excls[eid];
                int o = ex.i == i ? ex.j : ex.i;
                int so = start[o];
                if (so < 0) continue;
                int a = std::max(s, so);
                int b = std::min(s + I.delta[i][s], so + I.delta[o][so]);
                if (a < b && seasonHits(I.seasonMask[ex.season], a, b)) ++cf;
            }
            c += lamExcl * cf;
        }
        return c + noise;
    }

    void initialSolution() {
        std::vector<int> order(I.N);
        for (int i = 0; i < I.N; ++i) order[i] = i;
        for (int i = (int)order.size() - 1; i > 0; --i) {
            int j = rng.below(i + 1);
            std::swap(order[i], order[j]);
        }
        for (int i : order) {
            int bestS = 0;
            double bestC = std::numeric_limits<double>::infinity();
            for (int s = 0; s < I.tmax[i]; ++s) {
                double c = insertCost(i, s, 0);
                if (c < bestC) { bestC = c; bestS = s; }
            }
            place(i, bestS);
        }
        refreshAllQ();
    }

    // destroys
    void destroyRandom(int k) {
        removedList.clear();
        int guard = 0;
        while ((int)removedList.size() < k && guard++ < 20 * k) {
            int i = rng.below(I.N);
            if (start[i] < 0) continue;
            removedList.push_back(i);
            remove(i);
        }
    }

    void destroyWorst(int k) {
        removedList.clear();
        int sample = std::min(I.N, 120);
        std::vector<std::pair<double, int>> cands;
        for (int z = 0; z < sample; ++z) {
            int i = rng.below(I.N);
            if (start[i] < 0) continue;
            int s = start[i];
            double relief = I.alpha * (double)I.meanSum[i][s] / I.T;
            uint32_t b = I.wlOff[i][s], e = I.wlOff[i][s + 1];
            for (uint32_t kk = b; kk < e; ++kk)
                relief += lamRes * cellViol((int)I.wlRes[kk], (int)I.wlT[kk]);
            relief += lamExcl * conflictsOf(i);
            cands.push_back({relief, i});
        }
        std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (int z = 0; z < k && z < (int)cands.size(); ++z) {
            int i = cands[z].second;
            if (start[i] >= 0) { removedList.push_back(i); remove(i); }
        }
    }

    void destroyRelated(int k) {
        removedList.clear();
        int seedI = rng.below(I.N);
        int guard = 0;
        while (start[seedI] < 0 && guard++ < 50) seedI = rng.below(I.N);
        if (start[seedI] < 0) { destroyRandom(k); return; }
        int sSeed = start[seedI];
        std::vector<std::pair<double, int>> byDist;
        for (int i = 0; i < I.N; ++i)
            if (i != seedI && start[i] >= 0)
                byDist.push_back({std::abs((double)start[i] - sSeed), i});
        std::sort(byDist.begin(), byDist.end());
        removedList.push_back(seedI); remove(seedI);
        for (auto& pr : byDist) {
            if ((int)removedList.size() >= k) break;
            if (rng.unit() < 0.2) continue;
            removedList.push_back(pr.second);
            remove(pr.second);
        }
    }

    void destroyTimeBucket(int k) {
        removedList.clear();
        int t0 = rng.below(I.T);
        std::vector<std::pair<double, int>> byDist;
        for (int i = 0; i < I.N; ++i)
            if (start[i] >= 0)
                byDist.push_back({std::abs((double)start[i] - t0), i});
        std::sort(byDist.begin(), byDist.end());
        for (auto& pr : byDist) {
            if ((int)removedList.size() >= k) break;
            removedList.push_back(pr.second);
            remove(pr.second);
        }
    }

    void destroyExclusion(int k) {
        removedList.clear();
        if (I.excls.empty()) { destroyRandom(k); return; }
        const Excl& ex = I.excls[rng.below((int)I.excls.size())];
        if (start[ex.i] >= 0) { removedList.push_back(ex.i); remove(ex.i); }
        if (start[ex.j] >= 0) { removedList.push_back(ex.j); remove(ex.j); }
        int guard = 0;
        while ((int)removedList.size() < k && guard++ < 20 * k) {
            int i = rng.below(I.N);
            if (start[i] < 0) continue;
            removedList.push_back(i);
            remove(i);
        }
    }

    // remove the heaviest current users of a stressed (resource, time) cell
    void destroyResourceStress(int k) {
        removedList.clear();
        int br = -1, bt = -1;
        double bestV = -1;
        for (int tries = 24; tries > 0; --tries) {
            int r = rng.below(I.R), t = rng.below(I.T);
            double v = cellViol(r, t);
            if (v > bestV) { bestV = v; br = r; bt = t; }
            if (v > 0) break;
        }
        if (br < 0 || bestV <= 0) { destroyRandom(k); return; }
        const std::vector<uint32_t>& users = I.rtUsers[(size_t)br * I.T + bt];
        std::vector<std::pair<double, int>> contrib;
        for (uint32_t u : users) {
            int s = start[u];
            if (s < 0) continue;
            double w = 0;
            for (uint32_t kk = I.wlOff[u][s]; kk < I.wlOff[u][s + 1]; ++kk)
                if ((int)I.wlRes[kk] == br && (int)I.wlT[kk] == bt) w += I.wlW[kk];
            if (w > 0) contrib.push_back({w, u});
        }
        std::sort(contrib.begin(), contrib.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto& pr : contrib) {
            if ((int)removedList.size() >= k) break;
            removedList.push_back(pr.second);
            remove(pr.second);
        }
    }

    // repair with regret-2 greedy insertion; per-(candidate,start) costs are
    // cached and fully recomputed only every RECOMP_EVERY insertions (and
    // immediately for candidates exclusion-adjacent to the placed intervention)
    static const int RECOMP_EVERY = 3;
    static const int SCAN_CAP = 128; // start options evaluated per recompute
    std::vector<std::vector<double>> candCostBuf;
    std::vector<double> bestC, secondC;
    std::vector<int> bestS;
    std::vector<char> activeFlag;
    void repair(double noiseScale) { repairOp(noiseScale, 0); }

    // repair operators: 0 = regret-2 (cached, fresh before place), 1 = sequential
    // greedy (random order), 2 = noisy greedy, 3 = classic regret-2 with full
    // recompute after every insertion and per-start noise
    static const int NREP = 4;
    std::vector<double> repW;
    std::vector<uint64_t> repUsed, repScore;
    int lastRepairOp = 0;

    void repairOp(double noiseScale, int op) {
        lastRepairOp = op;
        std::vector<int>& L = removedList;
        int n = (int)L.size();
        if (n == 0) return;
        if ((int)candCostBuf.size() < n) candCostBuf.resize(n);
        bestC.assign(n, 0.0); secondC.assign(n, 0.0); bestS.assign(n, 0);
        activeFlag.assign(n, 1);
        double noise = (op == 2 || op == 3) ? noiseScale : noiseScale * 0.2;
        auto evalCand = [&](int z) {
            int i = L[z];
            int tm = I.tmax[i];
            int cnt = std::min(tm, SCAN_CAP);
            double b1 = std::numeric_limits<double>::infinity(), b2c = std::numeric_limits<double>::infinity();
            int bs = 0;
            for (int zq = 0; zq < cnt; ++zq) {
                int s = (tm <= SCAN_CAP) ? zq : rng.below(tm);
                // per-start independent noise (randomizes the choice among starts)
                double ns = noise > 0 ? rng.uniform(-noise, noise) : 0.0;
                double c = insertCost(i, s, ns);
                if (c < b1) { b2c = b1; b1 = c; bs = s; }
                else if (c < b2c) b2c = c;
            }
            bestC[z] = b1; secondC[z] = b2c; bestS[z] = bs;
        };
        if (op == 1 || op == 2) {
            // sequential greedy in random order, recompute each candidate at its turn
            std::vector<int> order(n);
            for (int z = 0; z < n; ++z) order[z] = z;
            for (int z = n - 1; z > 0; --z) {
                int j = rng.below(z + 1);
                std::swap(order[z], order[j]);
            }
            for (int z : order) {
                evalCand(z);
                place(L[z], bestS[z]);
            }
            return;
        }
        bool classic = (op == 3);
        for (int z = 0; z < n; ++z) evalCand(z);
        int remaining = n;
        int since = 0;
        while (remaining > 0) {
            int pick = -1; double bestReg = -1;
            for (int z = 0; z < n; ++z) {
                if (!activeFlag[z]) continue;
                double reg = secondC[z] - bestC[z];
                if (reg > bestReg) { bestReg = reg; pick = z; }
            }
            if (pick < 0) break;
            evalCand(pick); // fresh costs before committing (cached regrets only order)
            int placedI = L[pick];
            place(placedI, bestS[pick]);
            activeFlag[pick] = 0;
            --remaining;
            ++since;
            bool full = classic || (since >= RECOMP_EVERY);
            if (full) since = 0;
            for (int z = 0; z < n; ++z) {
                if (!activeFlag[z]) continue;
                bool adj = false;
                for (uint32_t eid : I.adjEx[L[z]]) {
                    const Excl& ex = I.excls[eid];
                    if (ex.i == placedI || ex.j == placedI) { adj = true; break; }
                }
                if (full || adj) evalCand(z);
            }
        }
    }

    int pickRepairOp() {
        double tot = 0;
        for (double w : repW) tot += w;
        double x = rng.unit() * tot;
        for (int z = 0; z < NREP; ++z) {
            x -= repW[z];
            if (x <= 0) { repUsed[z]++; return z; }
        }
        repUsed[NREP - 1]++;
        return NREP - 1;
    }

    // one greedy relocation sweep: for each intervention in random order,
    // move it to its cheapest start if that beats staying (approximate costs,
    // q held fixed); intended as periodic intensification
    void polishSweep() {
        std::vector<int> order(I.N);
        for (int i = 0; i < I.N; ++i) order[i] = i;
        for (int i = (int)order.size() - 1; i > 0; --i) {
            int j = rng.below(i + 1);
            std::swap(order[i], order[j]);
        }
        for (int i : order) {
            int sOld = start[i];
            if (sOld < 0) continue;
            remove(i);
            double cOld = insertCost(i, sOld, 0.0);
            int tm = I.tmax[i];
            int cnt = std::min(tm, SCAN_CAP);
            double cBest = cOld;
            int sBest = sOld;
            for (int zq = 0; zq < cnt; ++zq) {
                int s = (tm <= SCAN_CAP) ? zq : rng.below(tm);
                if (s == sOld) continue;
                double c = insertCost(i, s, 0.0);
                if (c < cBest - 1e-12) { cBest = c; sBest = s; }
            }
            place(i, sBest);
        }
    }

    int pickOp() {        double tot = 0;
        for (double w : opW) tot += w;
        double x = rng.unit() * tot;
        for (int z = 0; z < NOPS; ++z) {
            x -= opW[z];
            if (x <= 0) { opUsed[z]++; return z; }
        }
        opUsed[NOPS - 1]++;
        return NOPS - 1;
    }

    void run() {
        initialSolution();
        scale = std::max(1e-9, fabs(sampledObjective()));
        lamRes = scale;
        lamExcl = 100 * scale;
        {
            // calibrate SA start temperature from single-move deltas (v1 scheme)
            std::vector<double> deltas;
            deltas.reserve(48);
            for (int z = 0; z < 48; ++z) {
                int i = rng.below(I.N);
                int sold = start[i];
                remove(i);
                double c0 = sampledObjective() + lamRes * violRes + lamExcl * violExcl;
                int s = rng.below(I.tmax[i]);
                place(i, s);
                double c1 = sampledObjective() + lamRes * violRes + lamExcl * violExcl;
                deltas.push_back(fabs(c1 - c0));
                remove(i);
                place(i, sold);
            }
            refreshAllQ();
            std::sort(deltas.begin(), deltas.end());
            double med = deltas.empty() ? scale : deltas[deltas.size() / 2];
            T0 = std::max(med / 0.693, 1e-12);
        }
        refreshAllQ();
        std::vector<int> curStart = start;
        double curCost = totalCost();
        double t0Loop = now_sec();
        double span = std::max(1.0, deadline - t0Loop);
        Temp = T0;
        uint64_t iter = 0, sinceFeasible = 0;
        double lastLog = t0Loop;
        lastImprove = t0Loop;
        lastFeasibleT = t0Loop;
        lastFeasSwitch = t0Loop;
        int kmin = std::max(4, I.N / 100);
        int kmax = std::max(kmin + 1, std::min(I.N / 4, 60));
        double noiseBase = 0.5 * scale / std::max(1, I.N);
        double emaIter = 0.02; // EMA of seconds per iteration
        double lastT = now_sec();
        while (now_sec() < deadline && !shared.stopFlag.load(std::memory_order_relaxed)) {
            ++iter;
            double frac = (now_sec() - t0Loop) / span;
            Temp = T0 * std::pow(1e-5, std::max(0.0, std::min(1.0, frac)));
            if (feasOnly) Temp = std::min(Temp, 1e-3 * lamRes);
            {
                double nowT = now_sec();
                emaIter = 0.9 * emaIter + 0.1 * (nowT - lastT);
                lastT = nowT;
                int kcap = std::min(I.N / 4, 60);
                if (emaIter > 0.12 && kmax > kmin + 1) kmax -= std::max(1, kmax / 10);
                else if (emaIter < 0.02 && kmax < kcap) ++kmax;
                if (kmax < kmin + 1) kmax = kmin + 1;
            }
            int op = pickOp();
            int k = kmin + rng.below(std::max(1, kmax - kmin + 1));
            if (feasOnly) {
                op = rng.unit() < 0.5 ? 5 : 0; // resource-stress or random
                if (k > 12) k = 12;
            }
            switch (op) {
                case 0: destroyRandom(k); break;
                case 1: destroyWorst(k); break;
                case 2: destroyRelated(k); break;
                case 3: destroyTimeBucket(k); break;
                case 4: destroyExclusion(k); break;
                default: destroyResourceStress(k); break;
            }
            int rop = (emaIter > 0.10) ? 0 : 3; // quality-first; cached repair only when iterations are slow
            repUsed[rop]++;
            repairOp(noiseBase * (0.2 + rng.unit()), rop);
            refreshTouchedQ();
            double newCost = totalCost();
            bool accept = (newCost <= curCost) || (rng.unit() < std::exp(-(newCost - curCost) / Temp));
            if (accept) {
                curCost = newCost;
                curStart = start;
                if (violRes <= 1e-9 && violExcl == 0) {
                    lastFeasibleT = now_sec();
                    if (feasOnly) {
                        feasOnly = false;
                        curCost = totalCost();
                    }
                    double obj = sampledObjective();
                    if (obj < bestLocalSampled) {
                        bestLocalSampled = obj;
                        bestStartLocal = start;
                        lastImprove = now_sec();
                        opScore[op] += 3;
                        repScore[lastRepairOp] += 3;
                        {
                            std::lock_guard<std::mutex> g(shared.mu);
                            if (obj < shared.bestSampled) {
                                shared.bestSampled = obj;
                                shared.bestStart = start;
                                shared.improvedBy = id;
                            }
                        }
                    } else opScore[op] += 1;
                    repScore[lastRepairOp] += 1;
                    sinceFeasible = 0;
                } else {
                    // escalate penalties both by accepted-infeasible iterations and
                    // by wall time (slow instances never reach 3000 iterations)
                    double nwt = now_sec();
                    if (nwt - lastFeasibleT > 30.0) {
                        lamRes = std::min(scale * 1e6, lamRes * 1.3);
                        lamExcl = std::min(scale * 1e8, lamExcl * 1.15);
                        lastFeasibleT = nwt; // also gates the feasOnly switch below
                        curCost = totalCost();
                    }
                    if (++sinceFeasible > 3000) {
                        lamRes = std::min(scale * 1e6, lamRes * 1.5);
                        lamExcl = std::min(scale * 1e8, lamExcl * 1.2);
                        sinceFeasible = 0;
                        curCost = totalCost(); // rescale current cost reference
                    }
                    if (nwt - lastFeasSwitch > 90.0 && !feasOnly) {
                        feasOnly = true; // drop objective terms; minimize violations only
                        lastFeasSwitch = nwt;
                        curCost = totalCost();
                    }
                    double key = violExcl * 1e6 + violRes;
                    if (key < bestInfeasKey) { bestInfeasKey = key; bestInfeasStart = start; }
                }
            } else {
                for (int i = 0; i < I.N; ++i)
                    if (start[i] != curStart[i] && start[i] >= 0) remove(i);
                for (int i = 0; i < I.N; ++i)
                    if (curStart[i] >= 0 && start[i] < 0) place(i, curStart[i]);
                refreshTouchedQ();
            }
            // NOTE: a greedy relocation polish sweep was tested here and made
            // some instances get stuck infeasible (myopic joint moves); it is
            // disabled. Intensification is left to SA + restarts.
            if (now_sec() - lastImprove > 20.0) {
                std::vector<int> base;
                {
                    std::lock_guard<std::mutex> g(shared.mu);
                    if (!shared.bestStart.empty() && rng.unit() < 0.5) base = shared.bestStart;
                }
                if (base.empty() && !bestStartLocal.empty()) base = bestStartLocal;
                if (base.empty() && !bestInfeasStart.empty()) base = bestInfeasStart;
                if (base.empty()) base = curStart;
                for (int i = 0; i < I.N; ++i) if (start[i] >= 0) remove(i);
                for (int i = 0; i < I.N; ++i) place(i, base[i]);
                destroyRandom(kmax);
                repair(0.1 * noiseBase * 20);
                refreshTouchedQ();
                curStart = start;
                curCost = totalCost();
                lastImprove = now_sec();
                Temp = T0 * 0.3;
            }
            if (iter % 500 == 0) {
                for (int z = 0; z < NOPS; ++z) {
                    if (opUsed[z] > 0) opW[z] = 0.9 * opW[z] + 0.1 * ((double)opScore[z] / (double)opUsed[z]);
                    opScore[z] = 0; opUsed[z] = 0;
                    if (opW[z] < 0.05) opW[z] = 0.05;
                }
                for (int z = 0; z < NREP; ++z) {
                    if (repUsed[z] > 0) repW[z] = 0.9 * repW[z] + 0.1 * ((double)repScore[z] / (double)repUsed[z]);
                    repScore[z] = 0; repUsed[z] = 0;
                    if (repW[z] < 0.05) repW[z] = 0.05;
                }
            }
            if ((iter & 1023) == 0) {
                double tn = now_sec();
                if (tn - lastLog > 5.0) {
                    lastLog = tn;
                    fprintf(stderr, "[rep%d] t=%.0f iters=%llu violRes=%.4g violExcl=%d curObj=%.6g best=%.6g Temp=%.3g lamRes=%.3g\n",
                            id, tn - t0Loop, (unsigned long long)iter, violRes, violExcl,
                            sampledObjective(), bestLocalSampled, Temp, lamRes);
                }
            }
        }
        iterations = iter;
    }
};

// ------------------------------------------------------------------ driver

struct Args {
    std::string instance, solution, summary;
    uint64_t seed = 1;
    double budget = 870;
    int threads = 4;
    int samples = 224;
    bool stats = false;
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", k.c_str()); exit(2); }
            return argv[++i];
        };
        if (k == "--instance") a.instance = next();
        else if (k == "--solution") a.solution = next();
        else if (k == "--summary") a.summary = next();
        else if (k == "--seed") a.seed = strtoull(next().c_str(), nullptr, 10);
        else if (k == "--time-budget") a.budget = atof(next().c_str());
        else if (k == "--threads") a.threads = atoi(next().c_str());
        else if (k == "--samples") a.samples = atoi(next().c_str());
        else if (k == "--stats") a.stats = true;
        else { fprintf(stderr, "unknown arg %s\n", k.c_str()); exit(2); }
    }
    if (a.instance.empty()) { fprintf(stderr, "--instance required\n"); exit(2); }
    return a;
}

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);
    double t0 = now_sec();
    setvbuf(stdout, nullptr, _IOLBF, 0);
    Instance I;
    try {
        loadInstance(args.instance, I, args.seed, args.samples);
    } catch (JsonError& e) {
        fprintf(stderr, "PARSE_ERROR: %s\n", e.msg.c_str());
        return 3;
    } catch (std::exception& e) {
        fprintf(stderr, "PARSE_EXCEPTION: %s\n", e.what());
        return 3;
    }
    double tParse = now_sec() - t0;
    if (args.stats) {
        printf("{\"instance\":\"%s\",\"N\":%d,\"T\":%d,\"R\":%d,\"alpha\":%g,\"tau\":%g,"
               "\"exclusions\":%zu,\"scen_min\":%d,\"scen_max\":%d,"
               "\"risk_entries\":%llu,\"risk_numbers\":%llu,\"wl_entries\":%llu,"
               "\"mr_entries\":%llu,\"K\":%d,\"storeFull\":%s,\"parse_sec\":%.2f}\n",
               args.instance.c_str(), I.N, I.T, I.R, I.alpha, I.tau,
               I.excls.size(),
               *std::min_element(I.S.begin(), I.S.end()), *std::max_element(I.S.begin(), I.S.end()),
               (unsigned long long)I.riskEntries, (unsigned long long)I.riskNumbers,
               (unsigned long long)I.wlEntries, (unsigned long long)I.mrEntries,
               I.K, I.storeFull ? "true" : "false", tParse);
        return 0;
    }
    if (args.solution.empty()) { fprintf(stderr, "--solution required\n"); return 2; }

    double deadline = t0 + args.budget;
    Shared shared;
    int nthreads = std::max(1, args.threads);
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<Searcher>> searchers;
    for (int t = 0; t < nthreads; ++t)
        searchers.push_back(std::make_unique<Searcher>(I, args.seed * 1000003ULL + 7919ULL * (uint64_t)(t + 1), t, deadline, shared));
    for (int t = 0; t < nthreads; ++t)
        threads.emplace_back([&, t]() { searchers[t]->run(); });
    for (auto& th : threads) th.join();

    // final selection: prefer feasible; rank by exact objective when available
    std::vector<std::vector<int>> cands;
    {
        std::lock_guard<std::mutex> g(shared.mu);
        if (!shared.bestStart.empty()) cands.push_back(shared.bestStart);
    }
    for (auto& s : searchers) {
        if (!s->bestStartLocal.empty()) cands.push_back(s->bestStartLocal);
        if (cands.empty() && !s->start.empty()) cands.push_back(s->start);
    }
    std::sort(cands.begin(), cands.end());
    cands.erase(std::unique(cands.begin(), cands.end()), cands.end());

    struct Result { double score = 1e300; double violRes = 0; int violExcl = 0; std::vector<int> start; bool feasible = false; };
    Result best, fallback;
    for (auto& c : cands) {
        Diag d = diagnose(I, c);
        bool feas = (d.violRes <= 1e-5 && d.violExcl == 0);
        if (feas) {
            ExactEval ev = evaluateExact(I, c);
            double score = ev.ok ? ev.obj : sampledObjectiveFor(I, c);
            if (score < best.score) { best.score = score; best.start = c; best.feasible = true; best.violRes = d.violRes; best.violExcl = d.violExcl; }
        } else if (fallback.start.empty() ||
                   d.violExcl < fallback.violExcl ||
                   (d.violExcl == fallback.violExcl && d.violRes < fallback.violRes)) {
            fallback.violExcl = d.violExcl; fallback.violRes = d.violRes; fallback.start = c;
        }
    }
    bool haveFeasible = best.feasible;
    const std::vector<int>& outStart = haveFeasible ? best.start : fallback.start;
    if (outStart.empty()) { fprintf(stderr, "no candidate solution produced\n"); return 5; }

    {
        FILE* f = fopen(args.solution.c_str(), "w");
        if (!f) { fprintf(stderr, "cannot write solution\n"); return 4; }
        for (int i = 0; i < I.N; ++i)
            fprintf(f, "%s %d\n", I.iName[i].c_str(), outStart[i] + 1);
        fclose(f);
    }

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double tEnd = now_sec();
    long maxrssMB = ru.ru_maxrss / 1024;
    uint64_t iters = 0;
    for (auto& s : searchers) iters += s->iterations;
    FILE* sf = args.summary.empty() ? stdout : fopen(args.summary.c_str(), "w");
    if (sf) {
        fprintf(sf, "{\"instance\":\"%s\",\"seed\":%llu,\"feasible\":%s,"
                    "\"exact_obj\":%.6f,\"viol_res\":%.8f,\"viol_excl\":%d,"
                    "\"iters\":%llu,\"threads\":%d,\"K\":%d,\"storeFull\":%s,"
                    "\"parse_sec\":%.2f,\"total_sec\":%.2f,\"maxrss_mb\":%ld}\n",
                args.instance.c_str(), (unsigned long long)args.seed,
                haveFeasible ? "true" : "false",
                haveFeasible ? best.score : -1.0,
                haveFeasible ? best.violRes : fallback.violRes,
                haveFeasible ? best.violExcl : fallback.violExcl,
                (unsigned long long)iters, nthreads, I.K, I.storeFull ? "true" : "false",
                tParse, tEnd - t0, maxrssMB);
        if (!args.summary.empty()) fclose(sf);
    }
    return 0;
}
