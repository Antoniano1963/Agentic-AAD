// ROADEF2022 truck loading solver - from scratch
// v001: latest-first greedy assignment, homogeneous stacks, 2-lane packer, extra trucks.
#include <bits/stdc++.h>
#include <chrono>
using namespace std;
using Clock = chrono::steady_clock;

static Clock::time_point T0;
static double elapsed() { return chrono::duration<double>(Clock::now() - T0).count(); }
static double DEADLINE = 1e9;
static const double LS_TIME_CAP = 300.0;
static double g_lsDeadline = 1e9;
static long long g_extraSeq = 1000000;

static void logf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

// ---------------- interned strings ----------------
static unordered_map<string,int> g_strId;
static vector<string> g_str;
static int internStr(const string& s) {
    auto it = g_strId.find(s);
    if (it != g_strId.end()) return it->second;
    int id = (int)g_str.size();
    g_str.push_back(s);
    g_strId.emplace(s, id);
    return id;
}

struct Params { double aI = 1, aT = 1, aE = 0.2; };
static Params P;

struct ItemLine {
    string id;
    int sup = -1, plant = -1, pdock = -1, sdock = -1, prod = -1, sc = -1;
    int qty = 0, rem = 0, L = 0, W = 0, H = 0, nest = 0, maxst = 0, ic = 0, orient = 0;
    long long t0 = 0, t1 = 0; int t1day = 0;
    double wt = 0;
    vector<int> cand;
    int candCount = 0;
    int remCand = 0;
};

struct Truck {
    string id;
    int plant = -1, day = 0;
    long long arr = 0;
    int L = 0, W = 0, H = 0, maxwt = 0, maxdens = 0;
    bool md = false;
    int cost = 1500;
    double CM = 0, CJfm = 0, CJfc = 0, CJfh = 0, EM = 0, EJhr = 0, EJcr = 0, EJeh = 0, EMmm = 0, EMmr = 0;
    vector<int> sups, supOrds;
    unordered_map<int,int> supOrd, sdOrd, pdOrd;
    unordered_set<int> prods;
    unordered_map<int,int> maxAbove;
    vector<int> lines;
    bool extra = false;
    int baseIdx = -1;
    double area() const { return (double)L * (double)W / 1e6; }
};

static vector<ItemLine> g_items;
static vector<Truck> g_trucks;
static long long g_totalItems = 0;

// ---------------- dates ----------------
static int days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}
static long long parseTime12(const string& s) {
    long long v = 0; int n = 0;
    for (char c : s) { if (c < '0' || c > '9') break; v = v * 10 + (c - '0'); if (++n == 12) break; }
    return v;
}
static int ymdToDay(const string& s) {
    if (s.size() < 8) return 0;
    int y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    int m = (s[4]-'0')*10 + (s[5]-'0');
    int d = (s[6]-'0')*10 + (s[7]-'0');
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return days_from_civil(y, (unsigned)m, (unsigned)d);
}

// ---------------- parsing ----------------
static inline void stripSpaces(string& s) { s.erase(remove(s.begin(), s.end(), ' '), s.end()); }
static void splitLine(const string& s, char sep, vector<string>& out) {
    out.clear();
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == string::npos) { out.emplace_back(s.substr(start)); break; }
        out.emplace_back(s.substr(start, p - start));
        start = p + 1;
    }
}
static double parseDoubleComma(const string& s) {
    string t; t.reserve(s.size());
    for (char c : s) { if (c == ',') t.push_back('.'); else if (c != ' ') t.push_back(c); }
    if (t.empty()) return 0.0;
    try { return stod(t); } catch (...) { return 0.0; }
}
static int parseInt(const string& s) {
    try { return stoi(s); } catch (...) { return 0; }
}
static string padLeft(const string& s, int width) {
    if ((int)s.size() >= width) return s;
    return string(width - s.size(), '0') + s;
}
static bool readFileToString(const string& path, string& out) {
    ifstream f(path, ios::binary);
    if (!f) return false;
    f.seekg(0, ios::end);
    size_t n = (size_t)f.tellg();
    f.seekg(0, ios::beg);
    out.resize(n);
    if (n) f.read(&out[0], n);
    return true;
}
static void splitLines(const string& txt, vector<string>& lines) {
    lines.clear();
    size_t start = 0, n = txt.size();
    while (start < n) {
        size_t p = txt.find('\n', start);
        string ln;
        if (p == string::npos) { ln = txt.substr(start); start = n; }
        else { ln = txt.substr(start, p - start); start = p + 1; }
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        lines.push_back(std::move(ln));
    }
}

static void parseParameters(const string& path) {
    string txt; vector<string> lines, f;
    if (!readFileToString(path, txt)) { logf("cannot read %s\n", path.c_str()); return; }
    splitLines(txt, lines);
    for (size_t i = 1; i < lines.size(); i++) {
        string ln = lines[i]; stripSpaces(ln);
        if (ln.empty()) continue;
        splitLine(ln, ';', f);
        if (f.size() < 4) continue;
        P.aI = parseDoubleComma(f[0]);
        P.aT = parseDoubleComma(f[1]);
        P.aE = parseDoubleComma(f[2]);
        break;
    }
    logf("params aI=%g aT=%g aE=%g\n", P.aI, P.aT, P.aE);
}

static void parseItems(const string& path) {
    string txt; vector<string> lines, fields;
    if (!readFileToString(path, txt)) { logf("cannot read %s\n", path.c_str()); return; }
    splitLines(txt, lines);
    g_items.reserve(lines.size());
    for (size_t li = 1; li < lines.size(); li++) {
        const string& ln = lines[li];
        if (ln.find(';') == string::npos) continue;
        splitLine(ln, ';', fields);
        if (fields.empty() || fields[0].empty()) continue;
        ItemLine it;
        string id = fields[0]; stripSpaces(id);
        it.id = padLeft(id, 15);
        string sup = fields[1]; stripSpaces(sup);
        it.sup = internStr(padLeft(sup, 10));
        string sd = fields[2]; stripSpaces(sd);
        it.sdock = internStr(sd);
        string pl = fields[3]; stripSpaces(pl);
        it.plant = internStr(padLeft(pl, 10));
        string pd = fields[4]; stripSpaces(pd);
        it.pdock = internStr(pd);
        string pr = fields[5]; stripSpaces(pr);
        it.prod = internStr(pr);
        it.qty = parseInt(fields[7]);
        it.rem = it.qty;
        it.L = parseInt(fields[8]);
        it.W = parseInt(fields[9]);
        it.H = parseInt(fields[10]);
        it.wt = parseDoubleComma(fields[11]);
        it.nest = parseInt(fields[12]);
        string sc = fields[13]; stripSpaces(sc);
        it.sc = internStr(sc);
        string orient = fields[14]; stripSpaces(orient);
        for (auto& c : orient) c = (char)toupper((unsigned char)c);
        it.orient = (orient == "LENGTHWISE") ? 1 : (orient == "WIDTHWISE" ? 2 : 0);
        it.t0 = parseTime12(fields[15]);
        it.t1 = parseTime12(fields[16]);
        it.t1day = ymdToDay(fields[16]);
        it.ic = parseInt(fields[17]);
        it.maxst = parseInt(fields[18]);
        if (it.maxst <= 0) it.maxst = 1;
        if (it.qty <= 0) continue;
        g_totalItems += it.qty;
        g_items.push_back(std::move(it));
    }
    logf("items lines=%zu qty=%lld\n", g_items.size(), g_totalItems);
}

static void parseTrucks(const string& path) {
    string txt; vector<string> lines, fields;
    if (!readFileToString(path, txt)) { logf("cannot read %s\n", path.c_str()); return; }
    splitLines(txt, lines);
    unordered_map<string,int> idMap;
    idMap.reserve(8192);
    g_trucks.reserve(8192);
    for (size_t li = 1; li < lines.size(); li++) {
        const string& ln = lines[li];
        if (ln.find(';') == string::npos) continue;
        splitLine(ln, ';', fields);
        if (fields.empty() || fields[0].empty()) continue;
        string tid = fields[9]; stripSpaces(tid);
        if (tid.empty()) continue;
        auto itm = idMap.find(tid);
        int ti;
        if (itm == idMap.end()) {
            ti = (int)g_trucks.size();
            idMap.emplace(tid, ti);
            g_trucks.emplace_back();
            Truck& t = g_trucks.back();
            t.id = tid;
            string pl = fields[4]; stripSpaces(pl);
            t.plant = internStr(padLeft(pl, 10));
            t.arr = parseTime12(fields[8]);
            t.day = ymdToDay(fields[8]);
            t.L = parseInt(fields[10]);
            t.W = parseInt(fields[11]);
            t.H = parseInt(fields[12]);
            t.maxwt = parseInt(fields[13]);
            t.md = (parseInt(fields[14]) == 1);
            t.maxdens = parseInt(fields[15]);
            t.cost = parseInt(fields[17]);
            t.EMmm = parseDoubleComma(fields[18]);
            t.EMmr = parseDoubleComma(fields[19]);
            t.CM = parseDoubleComma(fields[20]);
            t.CJfm = parseDoubleComma(fields[21]);
            t.CJfc = parseDoubleComma(fields[22]);
            t.CJfh = parseDoubleComma(fields[23]);
            t.EM = parseDoubleComma(fields[24]);
            t.EJhr = parseDoubleComma(fields[25]);
            t.EJcr = parseDoubleComma(fields[26]);
            t.EJeh = parseDoubleComma(fields[27]);
        } else ti = itm->second;
        Truck& t = g_trucks[ti];
        string sup = fields[0]; stripSpaces(sup);
        int supId = internStr(padLeft(sup, 10));
        int so = parseInt(fields[1]);
        if (!t.supOrd.count(supId)) { t.supOrd.emplace(supId, so); t.sups.push_back(supId); t.supOrds.push_back(so); }
        string sd = fields[2]; stripSpaces(sd);
        int sdId = internStr(sd);
        if (!t.sdOrd.count(sdId)) t.sdOrd.emplace(sdId, parseInt(fields[3]));
        string pd = fields[5]; stripSpaces(pd);
        int pdId = internStr(pd);
        if (!t.pdOrd.count(pdId)) t.pdOrd.emplace(pdId, parseInt(fields[6]));
        string pr = fields[7]; stripSpaces(pr);
        int prId = internStr(pr);
        t.prods.insert(prId);
        t.maxAbove[prId] = parseInt(fields[16]);
    }
    logf("trucks=%zu\n", g_trucks.size());
}

// ---------------- compatibility ----------------
static inline int stackSize(const ItemLine& it, const Truck& t) {
    if (it.H > t.H) return 0;
    // must fit floor footprint in at least one orientation
    bool fit1 = (it.L <= t.L && it.W <= t.W);
    bool fit2 = (it.W <= t.L && it.L <= t.W);
    if (!fit1 && !fit2) return 0;
    int k = it.maxst;
    int dh = it.H - it.nest;
    if (dh > 0) {
        int hLim = 1 + (t.H - it.H) / dh;
        if (hLim < k) k = hLim;
    }
    double area = (double)it.L * (double)it.W / 1e6;
    if (area <= 0) return 0;
    if (it.wt > 0) {
        while (k > 0 && floor(k * it.wt / area + 1e-9) > t.maxdens) k--;
    }
    auto ma = t.maxAbove.find(it.prod);
    if (ma != t.maxAbove.end() && it.wt > 0) {
        int aboveLim = 1 + (int)floor((double)ma->second / it.wt + 1e-9);
        if (aboveLim < k) k = aboveLim;
    }
    if (k < 1) return 0;
    if (k > it.maxst) k = it.maxst;
    return k;
}

static inline bool compatible(const ItemLine& it, const Truck& t) {
    if (t.plant != it.plant) return false;
    if (!t.prods.count(it.prod)) return false;
    if (!t.supOrd.count(it.sup)) return false;
    if (!t.sdOrd.count(it.sdock)) return false;
    if (!t.pdOrd.count(it.pdock)) return false;
    if (t.arr < it.t0 || t.arr > it.t1) return false;
    return stackSize(it, t) > 0;
}

static void buildCandidates() {
    unordered_map<unsigned long long, vector<int>> bySP;
    bySP.reserve(g_trucks.size() * 32);
    for (int ti = 0; ti < (int)g_trucks.size(); ti++) {
        Truck& t = g_trucks[ti];
        for (int s : t.sups) {
            unsigned long long base = ((unsigned long long)(unsigned)s << 32);
            for (int p : t.prods) bySP[base | (unsigned)p].push_back(ti);
        }
    }
    long long edges = 0;
    for (int li = 0; li < (int)g_items.size(); li++) {
        ItemLine& it = g_items[li];
        auto itb = bySP.find(((unsigned long long)(unsigned)it.sup << 32) | (unsigned)it.prod);
        if (itb == bySP.end()) { it.candCount = 0; continue; }
        for (int ti : itb->second) if (compatible(it, g_trucks[ti])) it.cand.push_back(ti);
        it.candCount = (int)it.cand.size();
        it.remCand = it.candCount;
        for (int ti : it.cand) g_trucks[ti].lines.push_back(li);
        edges += it.candCount;
    }
    logf("candidate edges=%lld\n", edges);
    int zero = 0; long long zq = 0;
    for (auto& it : g_items) if (it.candCount == 0) { zero++; zq += it.qty; }
    logf("items with no candidate: lines=%d qty=%lld\n", zero, zq);
}

// ---------------- stacks & packing ----------------
struct StackComp { int line = -1; int qty = 0; };
struct Stack {
    vector<StackComp> comps;   // bottom to top
    int forcedOrient = 0;      // 0 free, 1 lengthwise, 2 widthwise
    int line = -1, qty = 0;
    int sup = 0, sdock = 0, pdock = 0, supOrd = 0, sdOrd = 0, pdOrd = 0;
    int L = 0, W = 0, H = 0, nest = 0, maxst = 0;
    double wt = 0, unitWt = 0;
    int orient = 0, depth = 0, yext = 0, height = 0;
    int x = 0, y = 0;
};

static vector<Stack> buildStacks(const Truck& t, const vector<pair<int,int>>& chunks) {
    struct Rem { int line; int qty; };
    vector<Rem> rems;
    rems.reserve(chunks.size());
    unordered_map<unsigned long long, vector<int>> fam;
    fam.reserve(chunks.size() * 2);
    for (auto& ch : chunks) {
        int idx = (int)rems.size();
        rems.push_back({ ch.first, ch.second });
        int sc = g_items[ch.first].sc;
        int su = g_items[ch.first].sup;
        int sd = g_items[ch.first].sdock;
        int pd = g_items[ch.first].pdock;
        unsigned long long key = ((unsigned long long)(unsigned)sc << 48) ^ ((unsigned long long)(unsigned)su << 32) ^
                                 ((unsigned long long)(unsigned)sd << 16) ^ (unsigned)pd;
        fam[key].push_back(idx);
    }
    vector<Stack> out;
    for (auto& kv : fam) {
        vector<int>& idxs = kv.second;
        while (true) {
            // bottom item: tallest remaining line in this family
            int b = -1; double bh = -1;
            for (int idx : idxs) {
                Rem& r = rems[idx];
                if (r.qty <= 0) continue;
                const ItemLine& it = g_items[r.line];
                if (stackSize(it, t) <= 0) continue;
                if ((double)it.H > bh) { bh = (double)it.H; b = idx; }
            }
            if (b < 0) break;
            Rem& rb = rems[b];
            const ItemLine& ib = g_items[rb.line];
            Stack st;
            st.line = rb.line;
            st.sup = ib.sup; st.sdock = ib.sdock; st.pdock = ib.pdock;
            st.supOrd = t.supOrd.at(ib.sup);
            st.sdOrd = t.sdOrd.at(ib.sdock);
            st.pdOrd = t.pdOrd.at(ib.pdock);
            st.L = ib.L; st.W = ib.W; st.H = ib.H; st.nest = ib.nest; st.maxst = ib.maxst;
            st.unitWt = ib.wt;
            int count = 1;
            int height = ib.H;
            double totalWt = ib.wt;
            double aboveWt = 0;
            int minMaxst = ib.maxst;
            int forced = ib.orient;
            st.comps.push_back({ rb.line, 1 });
            rb.qty--;
            double area = (double)ib.L * (double)ib.W / 1e6;
            double densLimit = (t.maxdens + 1.0) * area;
            double aboveLimit = 1e18;
            {
                auto ma = t.maxAbove.find(ib.prod);
                if (ma != t.maxAbove.end()) aboveLimit = (double)ma->second;
            }
            while (true) {
                int best = -1; double bestH = -1;
                for (int idx : idxs) {
                    Rem& r = rems[idx];
                    if (r.qty <= 0) continue;
                    const ItemLine& it = g_items[r.line];
                    if (it.L != ib.L || it.W != ib.W) continue;
                    if (height + (it.H - it.nest) > t.H) continue;
                    if (count + 1 > it.maxst || count + 1 > minMaxst) continue;
                    if (floor((totalWt + it.wt) / area + 1e-9) > t.maxdens) continue;
                    if (aboveWt + it.wt > aboveLimit + 1e-6) continue;
                    if (it.orient != 0) {
                        if (forced != 0 && forced != it.orient) continue;
                        if (forced == 0 && it.orient != 0) { /* will set forced below */ }
                    }
                    if ((double)it.H > bestH) { bestH = (double)it.H; best = idx; }
                }
                if (best < 0) break;
                Rem& r = rems[best];
                const ItemLine& it = g_items[r.line];
                if (forced == 0 && it.orient != 0) forced = it.orient;
                height += it.H - it.nest;
                totalWt += it.wt;
                aboveWt += it.wt;
                count++;
                minMaxst = min(minMaxst, it.maxst);
                if (!st.comps.empty() && st.comps.back().line == r.line) st.comps.back().qty++;
                else st.comps.push_back({ r.line, 1 });
                r.qty--;
            }
            st.qty = count;
            st.wt = totalWt;
            st.height = height;
            st.forcedOrient = forced;
            out.push_back(std::move(st));
        }
    }
    sort(out.begin(), out.end(), [](const Stack& a, const Stack& b) {
        if (a.supOrd != b.supOrd) return a.supOrd < b.supOrd;
        if (a.sdOrd != b.sdOrd) return a.sdOrd < b.sdOrd;
        if (a.pdOrd != b.pdOrd) return a.pdOrd < b.pdOrd;
        return a.wt < b.wt;
    });
    return out;
}

static void setOrient(Stack& s, int o) {
    s.orient = o;
    if (o == 1) { s.depth = s.L; s.yext = s.W; }
    else { s.depth = s.W; s.yext = s.L; }
}

struct AxleRes { bool ok = true; double worst = 0; };

static AxleRes checkAxle(const Truck& t, const vector<Stack>& stacks, const vector<char>& placed) {
    AxleRes res;
    vector<int> ords;
    for (size_t i = 0; i < stacks.size(); i++) if (placed[i]) ords.push_back(stacks[i].supOrd);
    sort(ords.begin(), ords.end());
    ords.erase(unique(ords.begin(), ords.end()), ords.end());
    for (int lim : ords) {
        double W = 0, mom = 0;
        for (size_t i = 0; i < stacks.size(); i++) {
            if (!placed[i]) continue;
            const Stack& s = stacks[i];
            if (s.supOrd > lim) continue;
            W += s.wt;
            mom += (s.x + s.depth * 0.5) * s.wt;
        }
        if (W <= 0) continue;
        double eje = mom / W;
        double ejr = t.EJeh + t.EJhr - eje;
        if (t.EJhr <= 0 || t.CJfm <= 0 || t.CJfh <= 0) continue;
        double emh = (W * ejr + t.EM * t.EJcr) / t.EJhr;
        double emr = W + t.EM - emh;
        double emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
        double r1 = emm / (t.EMmm > 0 ? t.EMmm : 1e9);
        double r2 = emr / (t.EMmr > 0 ? t.EMmr : 1e9);
        res.worst = max(res.worst, max(r1, r2));
        if (emm > t.EMmm + 0.1 || emr > t.EMmr + 0.1) res.ok = false;
    }
    return res;
}

// General 2D floor packer with support (P3) and class ordering (P4).
// mode 0: compact (smallest x first). mode 1: spread (largest x first, alternate lane side).
struct Rect { int x0, x1, y0, y1; };

static inline int cmpClass(const Stack& a, const Stack& b) {
    if (a.supOrd != b.supOrd) return a.supOrd < b.supOrd ? -1 : 1;
    if (a.sdOrd != b.sdOrd) return a.sdOrd < b.sdOrd ? -1 : 1;
    if (a.pdOrd != b.pdOrd) return a.pdOrd < b.pdOrd ? -1 : 1;
    return 0;
}

static vector<int> g_xc, g_yc;

static bool placeOne(const Truck& t, vector<Stack>& stacks, vector<Rect>& rects, int si, int mode, int lo, int hi) {
    Stack& s = stacks[si];
    int forced = s.forcedOrient;
    g_xc.clear();
    g_xc.push_back(0);
    for (auto& r : rects) g_xc.push_back(r.x1);
    sort(g_xc.begin(), g_xc.end());
    g_xc.erase(unique(g_xc.begin(), g_xc.end()), g_xc.end());
    if (mode == 1) reverse(g_xc.begin(), g_xc.end());
    for (int x : g_xc) {
        if (x < lo || x > hi) continue;
        bool hasSup = (x == 0);
        if (!hasSup) for (auto& r : rects) if (r.x1 == x) { hasSup = true; break; }
        if (!hasSup) continue;
        for (int oi = 0; oi < 2; oi++) {
            int o = oi + 1;
            if (forced && o != forced) continue;
            int depth = (o == 1) ? s.L : s.W;
            int yext = (o == 1) ? s.W : s.L;
            if (x + depth > t.L || yext > t.W) continue;
            g_yc.clear();
            g_yc.push_back(0);
            g_yc.push_back(t.W - yext);
            for (auto& r : rects) if (r.x1 == x) {
                g_yc.push_back(r.y0);
                g_yc.push_back(r.y0 - yext);
                g_yc.push_back(r.y1);
                g_yc.push_back(r.y1 - yext);
            }
            sort(g_yc.begin(), g_yc.end());
            g_yc.erase(unique(g_yc.begin(), g_yc.end()), g_yc.end());
            int bestY = -1, bestScore = INT_MAX;
            for (int y : g_yc) {
                if (y < 0 || y + yext > t.W) continue;
                int supScore = 2;
                bool sup = false;
                if (x == 0) { sup = true; supScore = 0; }
                else {
                    for (auto& r : rects) {
                        if (r.x1 != x) continue;
                        if (r.y1 < y || y + yext < r.y0) continue;
                        sup = true;
                        bool farSide = (y >= r.y1) || (y + yext <= r.y0);
                        if (farSide && supScore > 0) supScore = 0;
                        else if (supScore > 1) supScore = 1;
                    }
                }
                if (!sup) continue;
                bool ov = false;
                for (auto& r : rects) {
                    if (x < r.x1 && r.x0 < x + depth && y < r.y1 && r.y0 < y + yext) { ov = true; break; }
                }
                if (ov) continue;
                if (mode == 0) { bestY = y; break; }
                if (supScore < bestScore || (supScore == bestScore && (bestY < 0 || y < bestY))) {
                    bestScore = supScore; bestY = y;
                }
            }
            if (bestY >= 0) {
                setOrient(s, o);
                s.x = x; s.y = bestY;
                rects.push_back({ x, x + s.depth, bestY, bestY + s.yext });
                return true;
            }
        }
    }
    return false;
}

static vector<char> packGeneral(const Truck& t, vector<Stack>& stacks, int mode) {
    int n = (int)stacks.size();
    vector<char> placed(n, 0);
    vector<Rect> rects;
    rects.reserve(n);
    int M = 0;
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && cmpClass(stacks[j], stacks[i]) == 0) j++;
        for (; i < j; i++) {
            if (placeOne(t, stacks, rects, i, mode, M, t.L)) placed[i] = 1;
        }
        int mo = 0;
        for (int q = 0; q < i; q++) if (placed[q]) mo = max(mo, stacks[q].x);
        M = max(M, mo);
    }
    // insertion repair: try to fit unplaced stacks into remaining gaps
    int passes = (n <= 64) ? 2 : 1;
    for (int pass = 0; pass < passes; pass++) {
        bool any = false;
        for (int si = 0; si < n; si++) {
            if (placed[si]) continue;
            int lo = 0, hi = t.L;
            for (int q = 0; q < n; q++) {
                if (!placed[q]) continue;
                int c = cmpClass(stacks[q], stacks[si]);
                if (c < 0) lo = max(lo, stacks[q].x);
                else if (c > 0) hi = min(hi, stacks[q].x);
            }
            if (placeOne(t, stacks, rects, si, mode, lo, hi)) { placed[si] = 1; any = true; }
        }
        if (!any) break;
    }
    return placed;
}

static double totalWeight(const vector<Stack>& st, const vector<char>& placed) {
    double w = 0;
    for (size_t i = 0; i < st.size(); i++) if (placed[i]) w += st[i].wt;
    return w;
}

// ---------------- solution state ----------------
struct TruckLoad {
    vector<Stack> stacks;
    bool used = false;
};
static vector<TruckLoad> g_load;
static long long g_remaining = 0;
static const char* g_ctx = "?";
static long long g_spillPack = 0, g_spillAxle = 0, g_spillWt = 0, g_spillCnt = 0;
static double g_selArea = 0, g_keepArea = 0;
static long long g_failedTrucks = 0;

static void spillStack(const Stack& s) {
    if (s.comps.empty()) {
        g_items[s.line].rem += s.qty;
        g_remaining += s.qty;
        return;
    }
    for (auto& c : s.comps) {
        g_items[c.line].rem += c.qty;
        g_remaining += c.qty;
    }
}

// try to build a feasible load from given stacks; spills everything not kept.
static vector<Stack> layoutTruck(const Truck& t, vector<Stack> work, const char* dbg) {
    if (work.empty()) return {};
    vector<Stack> spilled;
    auto feasible = [&](const vector<Stack>& st, const vector<char>& p) -> bool {
        for (char c : p) if (!c) return false;
        vector<char> all(st.size(), 1);
        if (totalWeight(st, all) > t.maxwt + 0.1) return false;
        return checkAxle(t, st, all).ok;
    };
    for (int iter = 0; iter < 12 && !work.empty(); iter++) {
        vector<Stack> a = work;
        vector<char> pa = packGeneral(t, a, 0);
        int ca = 0; for (char c : pa) ca += c;
        if (feasible(a, pa)) {
            for (auto& s : spilled) spillStack(s);
            return a;
        }
        vector<Stack> b = work;
        vector<char> pb = packGeneral(t, b, 1);
        int cb = 0; for (char c : pb) cb += c;
        if (feasible(b, pb)) {
            for (auto& s : spilled) spillStack(s);
            return b;
        }
        if (ca == 0 && cb == 0) {
            for (auto& s : work) spilled.push_back(s);
            work.clear();
            break;
        }
        // prefer the attempt that placed more stacks; tie -> higher CoG (spread)
        auto cog = [](const vector<Stack>& st) {
            double w = 0, m = 0;
            for (auto& s : st) { w += s.wt; m += (s.x + s.depth * 0.5) * s.wt; }
            return w > 0 ? m / w : 0.0;
        };
        bool useA;
        if (ca != cb) useA = (ca > cb);
        else useA = cog(a) >= cog(b);
        vector<Stack>& chosen = useA ? a : b;
        vector<char>& pc = useA ? pa : pb;
        int cnt = useA ? ca : cb;
        if (cnt < (int)work.size()) {
            vector<Stack> nw;
            for (size_t i = 0; i < chosen.size(); i++) {
                if (pc[i]) nw.push_back(chosen[i]);
                else { spilled.push_back(chosen[i]); g_spillPack += chosen[i].qty; }
            }
            work.swap(nw);
            continue;
        }
        vector<char> all(chosen.size(), 1);
        double W = totalWeight(chosen, all);
        if (W > t.maxwt + 0.1) {
            int worst = -1; double bw = -1;
            for (size_t i = 0; i < chosen.size(); i++) if (chosen[i].wt > bw) { bw = chosen[i].wt; worst = (int)i; }
            spilled.push_back(chosen[worst]);
            g_spillWt += chosen[worst].qty;
            chosen.erase(chosen.begin() + worst);
            work = chosen;
            continue;
        }
        // drop a batch of the heaviest stacks of the earliest supplier order
        int minOrd = INT_MAX;
        for (auto& s : chosen) minOrd = min(minOrd, s.supOrd);
        int ndrop = max(1, (int)(chosen.size() * 0.12));
        for (int d = 0; d < ndrop && !chosen.empty(); d++) {
            int worst = -1; double bw = -1;
            for (size_t i = 0; i < chosen.size(); i++) {
                if (chosen[i].supOrd != minOrd) continue;
                if (chosen[i].wt > bw) { bw = chosen[i].wt; worst = (int)i; }
            }
            if (worst < 0) { worst = (int)chosen.size() - 1; }
            spilled.push_back(chosen[worst]);
            g_spillAxle += chosen[worst].qty;
            chosen.erase(chosen.begin() + worst);
        }
        work = chosen;
    }
    if (!work.empty()) { for (auto& s : work) spilled.push_back(s); work.clear(); }
    for (auto& s : spilled) spillStack(s);
    (void)dbg;
    return {};
}

// approximate max payload allowed by axle formula for a load of given length, full-truck prefix
static double axlePayloadAt(const Truck& t, double len) {
    double denom = t.CJfh;
    if (denom <= 0) return t.maxwt;
    double emhMax = (t.EMmm * t.CJfm - t.CM * t.CJfc) / denom;
    double ejr = t.EJeh + t.EJhr - len * 0.5;
    if (ejr <= 1.0) return t.maxwt;
    double W = (emhMax * t.EJhr - t.EM * t.EJcr) / ejr;
    if (W <= 0) return 0;
    return min(W, (double)t.maxwt);
}

// estimate a payload target from candidate pool stats and axle/length constraints
static double estimateTargetPayload(const Truck& t) {
    const vector<int>& lines = t.extra ? g_trucks[t.baseIdx].lines : t.lines;
    double sumW = 0, sumD = 0; int n = 0;
    for (int li : lines) {
        const ItemLine& it = g_items[li];
        if (it.rem <= 0) continue;
        int k = stackSize(it, t);
        if (k <= 0) continue;
        double y1 = it.L, y2 = it.W;
        double d;
        if (it.orient == 1) d = it.L;
        else if (it.orient == 2) d = it.W;
        else d = (it.L <= t.W / 2) ? it.W : it.L;
        sumW += k * it.wt; sumD += d; n++;
        if (n >= 96) break;
    }
    if (n == 0) return t.maxwt;
    double ws = sumW / n, d = max(1.0, sumD / n);
    double best = 0;
    int N1 = (int)floor(t.L / d);
    for (int N = N1; N >= 1; N--) {
        double W = N * ws;
        if (W <= axlePayloadAt(t, N * d)) { best = max(best, min(W, (double)t.maxwt)); break; }
    }
    int N2max = (int)floor(2.0 * t.L / d);
    for (int N = N2max; N >= 1; N--) {
        double len = N * d * 0.5;
        double W = N * ws;
        if (W <= axlePayloadAt(t, len)) { best = max(best, min(W, (double)t.maxwt)); break; }
    }
    if (best <= 0) best = t.maxwt * 0.4;
    return best * 0.97;
}

static vector<pair<int,int>> selectChunks(const Truck& t, double fillArea, double fillWt) {
    const vector<int>& lines = t.extra ? g_trucks[t.baseIdx].lines : t.lines;
    vector<pair<double,int>> pool;
    pool.reserve(lines.size());
    for (int li : lines) {
        ItemLine& it = g_items[li];
        if (it.rem <= 0) continue;
        int k = stackSize(it, t);
        if (k <= 0) continue;
        if (!compatible(it, t)) { logf("BADLINE ctx=%s ti-base=%d line=%d\n", g_ctx, t.baseIdx, li); continue; }
        double invDays = (double)(it.t1day - t.day) * it.ic;
        double sc = (double)it.candCount * 1000.0 + invDays;
        pool.emplace_back(sc, li);
    }
    sort(pool.begin(), pool.end(), [](const pair<double,int>& a, const pair<double,int>& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    double remArea = t.area() * fillArea;
    double remWt = t.maxwt * fillWt;
    // For multi-supplier trucks, the first picked-up supplier's stacks sit at the front;
    // cap their total weight so the axle constraint stays satisfiable.
    int minSupOrd = INT_MAX;
    for (int so : t.supOrds) minSupOrd = min(minSupOrd, so);
    bool multiSup = t.supOrds.size() >= 2;
    double capFirst = multiSup ? axlePayloadAt(t, 0.45 * t.L) * 0.95 : 1e18;
    double prefW1 = 0;
    vector<pair<int,int>> chunks;
    for (auto& pr : pool) {
        if (remArea <= 0.05 || remWt <= 10) break;
        int li = pr.second;
        ItemLine& it = g_items[li];
        if (it.rem <= 0) continue;
        int k = stackSize(it, t);
        if (k <= 0) continue;
        double area = (double)it.L * it.W / 1e6;
        if (area <= 0) continue;
        double hShare = (double)max(0, it.H - it.nest) / (double)max(1, t.H);
        if (hShare < 0.10) hShare = 0.10;
        double areaPerItem = area * hShare;
        int maxByArea = (int)floor(remArea / areaPerItem + 1e-9);
        int maxByWt = (it.wt > 0) ? (int)floor(remWt / it.wt + 1e-9) : maxByArea;
        int maxQty = min({ maxByArea, maxByWt, it.rem });
        if (multiSup && t.supOrd.at(it.sup) == minSupOrd && it.wt > 0) {
            double avail = capFirst - prefW1;
            if (avail < it.wt) continue;
            int capQty = (int)floor(avail / it.wt + 1e-9);
            maxQty = min(maxQty, capQty);
        }
        if (maxQty <= 0) continue;
        it.rem -= maxQty;
        g_remaining -= maxQty;
        chunks.emplace_back(li, maxQty);
        remArea -= (double)maxQty * areaPerItem;
        remWt -= (double)maxQty * it.wt;
        if (multiSup && t.supOrd.at(it.sup) == minSupOrd) prefW1 += (double)maxQty * it.wt;
    }
    return chunks;
}


// Try to build a feasible load (all stacks placed, weight & axle ok). Non-destructive.
static bool tryLayoutWith(const Truck& t, vector<Stack> stacks, vector<Stack>& out) {
    sort(stacks.begin(), stacks.end(), [](const Stack& a, const Stack& b) {
        if (a.supOrd != b.supOrd) return a.supOrd < b.supOrd;
        if (a.sdOrd != b.sdOrd) return a.sdOrd < b.sdOrd;
        if (a.pdOrd != b.pdOrd) return a.pdOrd < b.pdOrd;
        return a.wt < b.wt;
    });
    for (int attempt = 0; attempt < 2; attempt++) {
        vector<Stack> s = stacks;
        vector<char> p = packGeneral(t, s, attempt);
        bool all = true;
        for (char c : p) if (!c) { all = false; break; }
        if (!all) continue;
        vector<char> allm(s.size(), 1);
        if (totalWeight(s, allm) > t.maxwt + 0.1) continue;
        if (!checkAxle(t, s, allm).ok) continue;
        out = std::move(s);
        return true;
    }
    return false;
}

static bool stackCompatible(const Stack& st, const Truck& t) {
    if (st.comps.empty()) return false;
    for (auto& c : st.comps) {
        const ItemLine& it = g_items[c.line];
        if (t.plant != it.plant) return false;
        if (!t.prods.count(it.prod)) return false;
        if (!t.supOrd.count(it.sup)) return false;
        if (!t.sdOrd.count(it.sdock)) return false;
        if (!t.pdOrd.count(it.pdock)) return false;
        if (t.arr < it.t0 || t.arr > it.t1) return false;
    }
    if (st.height > t.H) return false;
    double area = (double)st.L * (double)st.W / 1e6;
    if (area > 0) {
        if (floor(st.wt / area + 1e-9) > t.maxdens) return false;
    }
    const ItemLine& bottom = g_items[st.comps[0].line];
    auto ma = t.maxAbove.find(bottom.prod);
    if (ma != t.maxAbove.end()) {
        double above = st.wt - bottom.wt;
        if (above > (double)ma->second + 0.1) return false;
    }
    return true;
}

static bool tryAddStack(int ti, const Stack& stIn) {
    Truck& t = g_trucks[ti];
    if (!stackCompatible(stIn, t)) {
        const ItemLine& it = g_items[stIn.line];
        logf("INCOMPAT ctx=%s ti=%d id=%s line=%d prod=%s arr=%lld win=[%lld,%lld]\n", g_ctx, ti, t.id.c_str(), stIn.line,
             g_str[it.prod].c_str(), t.arr, it.t0, it.t1);
        return false;
    }
    // class loading orders are truck-specific: recompute for the destination
    auto isup = t.supOrd.find(stIn.sup);
    auto isd = t.sdOrd.find(stIn.sdock);
    auto ipd = t.pdOrd.find(stIn.pdock);
    if (isup == t.supOrd.end() || isd == t.sdOrd.end() || ipd == t.pdOrd.end()) return false;
    Stack st = stIn;
    st.supOrd = isup->second; st.sdOrd = isd->second; st.pdOrd = ipd->second;
    vector<Stack> base = g_load[ti].stacks;
    base.push_back(st);
    vector<Stack> out;
    if (!tryLayoutWith(t, std::move(base), out)) return false;
    g_load[ti].stacks = std::move(out);
    g_load[ti].used = true;
    return true;
}

static Stack makeStack(int li, const Truck& t, int q) {
    const ItemLine& it = g_items[li];
    Stack st;
    st.line = li; st.qty = q;
    st.sup = it.sup; st.sdock = it.sdock; st.pdock = it.pdock;
    st.supOrd = t.supOrd.at(it.sup);
    st.sdOrd = t.sdOrd.at(it.sdock);
    st.pdOrd = t.pdOrd.at(it.pdock);
    st.L = it.L; st.W = it.W; st.H = it.H; st.nest = it.nest; st.maxst = it.maxst;
    st.unitWt = it.wt; st.wt = it.wt * q;
    st.height = it.H + (q - 1) * (it.H - it.nest);
    st.comps.clear();
    st.comps.push_back({ li, q });
    st.forcedOrient = it.orient;
    return st;
}

// Repair leftover quantities by inserting stacks into any planned truck that can accept them.
static void repairLeftovers() {
    g_ctx = "repair";
    if (g_remaining <= 0) return;
    if (g_remaining > 6000) { logf("skip repair (remaining=%lld t=%.1f)\n", g_remaining, elapsed()); return; }
    double repairDeadline = min(DEADLINE - 2.0, elapsed() + 20.0);
    vector<int> remLines;
    for (int i = 0; i < (int)g_items.size(); i++) if (g_items[i].rem > 0) remLines.push_back(i);
    sort(remLines.begin(), remLines.end(), [](int a, int b) {
        const ItemLine& ia = g_items[a]; const ItemLine& ib = g_items[b];
        if (ia.candCount != ib.candCount) return ia.candCount < ib.candCount;
        return ia.rem > ib.rem;
    });
    long long inserted = 0;
    for (int round = 0; round < 4 && g_remaining > 0; round++) {
        if (elapsed() > repairDeadline) break;
        bool progress = false;
        for (int li : remLines) {
            ItemLine& it = g_items[li];
            if (it.rem <= 0) continue;
            // candidates: used trucks first, later day first
            vector<int> cand = it.cand;
            sort(cand.begin(), cand.end(), [](int a, int b) {
                bool ua = g_load[a].used, ub = g_load[b].used;
                if (ua != ub) return ua > ub;
                if (g_trucks[a].day != g_trucks[b].day) return g_trucks[a].day > g_trucks[b].day;
                return g_trucks[a].area() > g_trucks[b].area();
            });
            for (int ti : cand) {
                Truck& t = g_trucks[ti];
                int k = stackSize(it, t);
                if (k <= 0) continue;
                int q = min(it.rem, k);
                Stack st = makeStack(li, t, q);
                if (tryAddStack(ti, st)) {
                    it.rem -= q; g_remaining -= q; inserted += q; progress = true;
                    break;
                }
                if (g_remaining <= 0) break;
            }
            if (g_remaining <= 0) break;
            if (elapsed() > repairDeadline) break;
        }
        if (!progress) break;
    }
    logf("repair inserted=%lld remaining=%lld t=%.1f\n", inserted, g_remaining, elapsed());
}


static void debugLeftovers() {
    int shown = 0;
    for (int li = 0; li < (int)g_items.size() && shown < 8; li++) {
        ItemLine& it = g_items[li];
        if (it.rem <= 0) continue;
        shown++;
        logf("LEFTOVER line=%d qty=%d cand=%d sup=%s prod=%s t=[%lld,%lld] LWH=%d,%d,%d wt=%.0f maxst=%d\\n",
             li, it.rem, it.candCount, g_str[it.sup].c_str(), g_str[it.prod].c_str(), it.t0, it.t1, it.L, it.W, it.H, it.wt, it.maxst);
        for (int ti : it.cand) {
            const Truck& t = g_trucks[ti];
            auto& st = g_load[ti].stacks;
            double A = 0, W = 0; int len = 0;
            for (auto& sq : st) { A += (double)sq.L * sq.W / 1e6; W += sq.wt; len = max(len, sq.x + sq.depth); }
            int k = stackSize(it, t);
            Stack ns = makeStack(li, t, min(it.rem, max(1, k)));
            vector<Stack> base = st; base.push_back(ns); vector<Stack> out;
            bool addok = tryLayoutWith(t, base, out);
            // reason
            vector<Stack> tmp = base;
            sort(tmp.begin(), tmp.end(), [](const Stack& a, const Stack& b) {
                if (a.supOrd != b.supOrd) return a.supOrd < b.supOrd;
                if (a.sdOrd != b.sdOrd) return a.sdOrd < b.sdOrd;
                if (a.pdOrd != b.pdOrd) return a.pdOrd < b.pdOrd;
                return a.wt < b.wt; });
            bool packAll = false; double wt = 0; bool axleOk = false;
            for (int mode = 0; mode < 2; mode++) {
                vector<Stack> q = tmp; vector<char> p = packGeneral(t, q, mode);
                bool all = true; for (char c : p) if (!c) { all = false; break; }
                double w = 0; for (auto& sq : q) w += sq.wt;
                vector<char> allm(q.size(), 1);
                bool ax = checkAxle(t, q, allm).ok;
                if (all && w <= t.maxwt + 0.1 && ax) packAll = true;
                if (all) { axleOk = ax; wt = w; }
            }
            logf("   cand ti=%d used=%d day=%d area=%.1f/%.1f W=%.0f/%.0f nst=%d len=%d k=%d add=%d fullok=%d\\n",
                 ti, (int)g_load[ti].used, t.day, A, t.area(), W, (double)t.maxwt, (int)st.size(), len, k, (int)addok, (int)packAll);
        }
    }
}


// Emergency fallback: one single-stack extra truck per remaining stack. Always completes.
static void fastFallback() {
    logf("FASTFALLBACK start remaining=%lld t=%.1f\n", g_remaining, elapsed());
    int made = 0;
    vector<int> extraCount(g_trucks.size(), 0);
    for (int li = 0; li < (int)g_items.size(); li++) {
        ItemLine& it = g_items[li];
        if (it.rem <= 0) continue;
        if (it.cand.empty()) { logf("ERROR no candidate line=%d qty=%d\n", li, it.rem); g_remaining -= it.rem; it.rem = 0; continue; }
        while (it.rem > 0) {
            int base = -1; int bestDay = -1;
            for (int ti : it.cand) { if (g_trucks[ti].day > bestDay) { bestDay = g_trucks[ti].day; base = ti; } }
            if (base < 0) break;
            Truck et = g_trucks[base];
            et.extra = true; et.baseIdx = base;
            int num = ++extraCount[base];
            et.id = "Q" + g_trucks[base].id.substr(1) + "_" + to_string(num);
            int ti = (int)g_trucks.size();
            g_trucks.push_back(et);
            g_load.emplace_back();
            int k = stackSize(it, g_trucks[ti]);
            if (k <= 0) { logf("ERROR stacksize0 line=%d base=%d\n", li, base); break; }
            int q = min(it.rem, k);
            Stack st = makeStack(li, g_trucks[ti], q);
            int o = (it.orient == 1) ? 1 : (it.orient == 2 ? 2 : ((it.L <= g_trucks[ti].W) ? 2 : 1));
            setOrient(st, o);
            if (st.depth > g_trucks[ti].L || st.yext > g_trucks[ti].W) {
                o = (o == 1) ? 2 : 1;
                setOrient(st, o);
            }
            st.x = 0; st.y = 0;
            g_load[ti].stacks.push_back(st);
            g_load[ti].used = true;
            it.rem -= q; g_remaining -= q; made++;
        }
    }
    logf("FASTFALLBACK done trucks=%d remaining=%lld t=%.1f\n", made, g_remaining, elapsed());
}


static int g_plannedTrucks = 0;
struct Snapshot {
    vector<TruckLoad> load;
    vector<Truck> extras;
    int nTrucks = 0;
};
static Snapshot snapshotSol() {
    Snapshot s;
    s.load = g_load;
    s.nTrucks = (int)g_trucks.size();
    s.extras.assign(g_trucks.begin() + g_plannedTrucks, g_trucks.end());
    return s;
}
static void resetState() {
    g_trucks.resize(g_plannedTrucks);
    g_load.assign(g_plannedTrucks, TruckLoad());
    for (auto& it : g_items) { it.rem = it.qty; it.remCand = it.candCount; }
    g_remaining = g_totalItems;
    g_spillPack = g_spillAxle = g_spillWt = 0;
    g_failedTrucks = 0;
}
static void restoreSol(const Snapshot& s) {
    g_load = s.load;
    g_trucks.resize(g_plannedTrucks);
    g_trucks.insert(g_trucks.end(), s.extras.begin(), s.extras.end());
    for (auto& it : g_items) it.rem = 0;
    g_remaining = 0;
}
static void construct(double fillArea) {
    g_load.assign(g_trucks.size(), TruckLoad());
    g_remaining = g_totalItems;
    vector<int> order(g_trucks.size());
    for (int i = 0; i < (int)order.size(); i++) order[i] = i;
    sort(order.begin(), order.end(), [](int a, int b) {
        const Truck& ta = g_trucks[a]; const Truck& tb = g_trucks[b];
        if (ta.day != tb.day) return ta.day > tb.day;   // latest first
        double aa = ta.area(), ab = tb.area();
        if (aa != ab) return aa > ab;
        return ta.id < tb.id;
    });
    double fillWt = 1.0;
    int usedCnt = 0, tried = 0;
    double tStart = elapsed();
    for (int ti : order) {
        if (g_remaining <= 0) break;
        Truck& t = g_trucks[ti];
        for (int li : t.lines) if (g_items[li].remCand > 0) g_items[li].remCand--;
        auto chunks = selectChunks(t, fillArea, fillWt);
        if (chunks.empty()) continue;
        tried++;
        for (auto& ch : chunks) { const ItemLine& ii = g_items[ch.first]; int kk = stackSize(ii, t); g_selArea += (double)ch.second / kk * ii.L * ii.W / 1e6; }
        auto stacks = buildStacks(t, chunks);
        auto kept = layoutTruck(t, std::move(stacks), "main");
        for (auto& s : kept) g_keepArea += (double)s.L * s.W / 1e6;
        if (kept.empty()) g_failedTrucks++;
        if (!kept.empty()) {
            g_load[ti].stacks = std::move(kept);
            g_load[ti].used = true;
            usedCnt++;
        }
        if (tried % 1000 == 0) logf("  construct ti=%d used=%d remaining=%lld t=%.1f\n", tried, usedCnt, g_remaining, elapsed());
    }
    logf("construct planned done: used=%d failed=%lld remaining=%lld selArea=%.0f keepArea=%.0f t=%.1f\n", usedCnt, g_failedTrucks, g_remaining, g_selArea, g_keepArea, elapsed());
    repairLeftovers();
    if (elapsed() > DEADLINE) logf("WARN deadline reached after construction\n");

    // extra trucks for leftovers
    int extras = 0;
    if (g_remaining > 0 && elapsed() > DEADLINE - 3.0) { fastFallback(); g_remaining = 0; }
    vector<int> extraCount(g_trucks.size(), 0);
    while (g_remaining > 0) {
        // pick the line with the largest remaining qty
        int li = -1; int bestQ = -1;
        for (int i = 0; i < (int)g_items.size(); i++) {
            if (g_items[i].rem > bestQ) { bestQ = g_items[i].rem; li = i; }
        }
        if (li < 0) break;
        ItemLine& it = g_items[li];
        // pick base truck: latest day, then largest remaining compatible pool, then area
        int base = -1; long long bestPool = -1; int bestDay = -1; double bestArea = -1;
        for (int ti : it.cand) {
            const Truck& tt = g_trucks[ti];
            long long pool = 0;
            for (int lj : tt.lines) pool += g_items[lj].rem;
            if (tt.day > bestDay || (tt.day == bestDay && (pool > bestPool || (pool == bestPool && tt.area() > bestArea)))) {
                bestDay = tt.day; bestPool = pool; bestArea = tt.area(); base = ti;
            }
        }
        if (base < 0) { logf("ERROR line %d has no candidate, qty=%d\n", li, it.rem); break; }
        Truck et = g_trucks[base];
        et.extra = true; et.baseIdx = base;
        int num = ++extraCount[base];
        et.id = "Q" + g_trucks[base].id.substr(1) + "_" + to_string(num);
        int ti = (int)g_trucks.size();
        g_trucks.push_back(et);
        g_load.emplace_back();
        auto chunks = selectChunks(g_trucks[ti], fillArea, fillWt);
        auto stacks = buildStacks(g_trucks[ti], chunks);
        auto kept = layoutTruck(g_trucks[ti], std::move(stacks), "extra");
        if (kept.empty()) {
            // guarantee progress: force one stack of the chosen line
            int k = stackSize(it, g_trucks[ti]);
            if (k <= 0) { logf("ERROR line %d incompatible with base %d\n", li, base); continue; }
            int q = min(it.rem, k);
            Stack st = makeStack(li, g_trucks[ti], q);
            vector<Stack> one{ st }, out;
            if (tryLayoutWith(g_trucks[ti], one, out)) {
                it.rem -= q; g_remaining -= q;
                g_load[ti].stacks = std::move(out);
                g_load[ti].used = true;
                extras++;
            } else {
                logf("ERROR cannot place single forced stack line=%d ti=%d\n", li, ti);
                break;
            }
        } else {
            g_load[ti].stacks = std::move(kept);
            g_load[ti].used = true;
            extras++;
            for (auto& st : g_load[ti].stacks) {
                if (!stackCompatible(st, g_trucks[ti])) {
                    bool inb = false;
                    for (int l : g_trucks[g_trucks[ti].baseIdx].lines) if (l == st.line) { inb = true; break; }
                    logf("EXTRA-BAD ti=%d id=%s base=%d line=%d inbase=%d\n", ti, g_trucks[ti].id.c_str(),
                         g_trucks[ti].baseIdx, st.line, (int)inb);
                    break;
                }
            }
        }
        if (extras % 200 == 0) logf("  extras=%d remaining=%lld t=%.1f\n", extras, g_remaining, elapsed());
        if (elapsed() > DEADLINE) {
            logf("DEADLINE during extras remaining=%lld, using fallback\n", g_remaining);
            fastFallback();
            g_remaining = 0;
            break;
        }
    }
    if (g_remaining > 0) { fastFallback(); g_remaining = 0; }
    logf("extras=%d remaining=%lld t=%.1f spillsPack=%lld spillsAxle=%lld spillsWt=%lld\n", extras, g_remaining, elapsed(), g_spillPack, g_spillAxle, g_spillWt);
    // diagnostics: used truck weight/area/axle-ratio histogram
    {
        int bins[10] = {0};
        double sumW = 0, sumA = 0; int used = 0;
        int tight = 0, near = 0;
        for (int i = 0; i < (int)g_trucks.size(); i++) {
            if (!g_load[i].used || g_load[i].stacks.empty()) continue;
            used++;
            const Truck& t = g_trucks[i];
            vector<char> all(g_load[i].stacks.size(), 1);
            double W = totalWeight(g_load[i].stacks, all);
            double A = 0; int len = 0;
            for (auto& s : g_load[i].stacks) { A += (double)s.L * s.W / 1e6; len = max(len, s.x + s.depth); }
            double mom = 0; for (auto& s : g_load[i].stacks) mom += (s.x + s.depth * 0.5) * s.wt;
            double cog = mom / W;
            AxleRes ar = checkAxle(t, g_load[i].stacks, all);
            sumW += W; sumA += A;
            int b = min(9, (int)(ar.worst * 10)); bins[b]++;
            if (ar.worst > 0.999) tight++;
            else if (ar.worst > 0.95) near++;
        }
        logf("DIAG used=%d avgW=%.0f avgA=%.1f tight=%d near=%d bins=", used, sumW / max(1, used), sumA / max(1, used), tight, near);
        for (int b = 0; b < 10; b++) logf("%d:%d ", b, bins[b]);
        logf("\\n");
    }
}

// ---------------- output ----------------
static string fmt2(double v) {
    char b[64];
    snprintf(b, sizeof(b), "%.2f", v);
    for (char* p = b; *p; p++) if (*p == '.') *p = ',';
    return string(b);
}
static string stackCode(int idx) {
    string s;
    int n = idx;
    do {
        s.push_back((char)('A' + n % 26));
        n = n / 26 - 1;
    } while (n >= 0);
    reverse(s.begin(), s.end());
    return s;
}

static double truckVolume(const vector<Stack>& stacks) {
    double v = 0;
    for (auto& s : stacks) {
        bool first = true;
        for (auto& c : s.comps) {
            const ItemLine& it = g_items[c.line];
            double area = (double)it.L * it.W;
            if (first) {
                v += area * it.H / 1e9;
                if (c.qty > 1) v += (c.qty - 1) * area * (it.H - it.nest) / 1e9;
                first = false;
            } else {
                v += c.qty * area * (it.H - it.nest) / 1e9;
            }
        }
    }
    return v;
}

static void writeOutput(const string& outDir) {
    // count used
    vector<int> used;
    for (int i = 0; i < (int)g_trucks.size(); i++) if (g_load[i].used && !g_load[i].stacks.empty()) used.push_back(i);
    logf("writing %zu used trucks\n", used.size());
    // trucks file
    {
        ofstream f(outDir + "/output_trucks.csv", ios::binary);
        f << "Id truck;Loaded length;Weight of loaded items;Volume of loaded items;emm;emr\n";
        char buf[512];
        for (int ti : used) {
            const Truck& t = g_trucks[ti];
            auto& st = g_load[ti].stacks;
            int len = 0; double W = 0;
            for (auto& s : st) { len = max(len, s.x + s.depth); W += s.wt; }
            double V = truckVolume(st);
            // emm/emr with all stacks
            double mom = 0;
            for (auto& s : st) mom += (s.x + s.depth * 0.5) * s.wt;
            double eje = mom / W;
            double ejr = t.EJeh + t.EJhr - eje;
            double emh = (W * ejr + t.EM * t.EJcr) / t.EJhr;
            double emr = W + t.EM - emh;
            double emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
            string sv = fmt2(V), se = fmt2(emm), sr = fmt2(emr), sw = fmt2(W);
            snprintf(buf, sizeof(buf), "%s;%d;%s;%s;%s;%s\n", t.id.c_str(), len, sw.c_str(), sv.c_str(), se.c_str(), sr.c_str());
            f << buf;
        }
    }
    // stacks and items
    ofstream fs(outDir + "/output_stacks.csv", ios::binary);
    ofstream fi(outDir + "/output_items.csv", ios::binary);
    fs << "Id truck;Id stack;Stack code;X origin;Y origin;Z origin;X extremity;Y extremity;Z extremity\n";
    fi << "Item ident;Id truck;Id stack;Item code;X origin;Y origin;Z origin;X extremity;Y extremity;Z extremity\n";
    string sbuf, ibuf;
    sbuf.reserve(1 << 20); ibuf.reserve(1 << 20);
    char b[512];
    for (int ti : used) {
        const Truck& t = g_trucks[ti];
        auto& st = g_load[ti].stacks;
        vector<int> ord(st.size());
        for (int i = 0; i < (int)st.size(); i++) ord[i] = i;
        sort(ord.begin(), ord.end(), [&](int a, int b) {
            if (st[a].x != st[b].x) return st[a].x < st[b].x;
            return st[a].y < st[b].y;
        });
        for (int k = 0; k < (int)ord.size(); k++) {
            Stack& s = st[ord[k]];
            string sid = t.id + "_" + to_string(k + 1);
            string code = stackCode(k);
            int xo = s.x, yo = s.y, xe = s.x + s.depth, ye = s.y + s.yext;
            int zh = s.height;
            snprintf(b, sizeof(b), "%s;%s;%s;%d;%d;0;%d;%d;%d\n", t.id.c_str(), sid.c_str(), code.c_str(), xo, yo, xe, ye, zh);
            sbuf += b;
            // items bottom to top
            {
                int z = 0; int itemNo = 0; bool first = true;
                for (auto& c : s.comps) {
                    const ItemLine& it = g_items[c.line];
                    for (int q = 0; q < c.qty; q++) {
                        int zo, ze;
                        if (first) { zo = 0; ze = it.H; first = false; }
                        else { zo = z; ze = zo + it.H - it.nest; }
                        z = ze; itemNo++;
                        snprintf(b, sizeof(b), "%s;%s;%s;%s%d;%d;%d;%d;%d;%d;%d\n",
                                 it.id.c_str(), t.id.c_str(), sid.c_str(), code.c_str(), itemNo,
                                 xo, yo, zo, xe, ye, ze);
                        ibuf += b;
                        if (ibuf.size() > (1 << 20)) { fi << ibuf; ibuf.clear(); }
                    }
                }
            }
            if (sbuf.size() > (1 << 20)) { fs << sbuf; sbuf.clear(); }
        }
    }
    fs << sbuf; fi << ibuf;
}

static double computeObjective(long long* trucksOut, double* invOut) {
    double transport = 0, inventory = 0;
    long long nt = 0;
    for (int i = 0; i < (int)g_trucks.size(); i++) {
        if (!g_load[i].used || g_load[i].stacks.empty()) continue;
        const Truck& t = g_trucks[i];
        nt++;
        transport += t.extra ? t.cost * (1.0 + P.aE) : t.cost;
        for (auto& s : g_load[i].stacks) {
            for (auto& c : s.comps) {
                const ItemLine& it = g_items[c.line];
                inventory += (double)it.ic * (double)(it.t1day - t.day) * c.qty;
            }
        }
    }
    if (trucksOut) *trucksOut = nt;
    if (invOut) *invOut = inventory;
    return P.aT * transport + P.aI * inventory;
}


// ---------------- local search ----------------
static inline double truckInvCost(const Truck& t, const vector<Stack>& st) {
    double inv = 0;
    for (auto& s : st) {
        for (auto& c : s.comps) {
            const ItemLine& it = g_items[c.line];
            inv += (double)it.ic * (double)(it.t1day - t.day) * c.qty;
        }
    }
    return inv;
}
static inline double truckFixedCost(const Truck& t) {
    return (t.extra ? t.cost * (1.0 + P.aE) : (double)t.cost);
}

struct SavedLoad { int ti; vector<Stack> stacks; bool used; };

// Try to move one stack from src to dest. Commits if both stay feasible.
static bool moveStackTo(int srcTi, int idx, int destTi, vector<SavedLoad>& undo) {
    Truck& src = g_trucks[srcTi];
    Stack st = g_load[srcTi].stacks[idx];
    auto save = [&](int ti) {
        for (auto& u : undo) if (u.ti == ti) return;
        undo.push_back({ ti, g_load[ti].stacks, g_load[ti].used });
    };
    save(srcTi); save(destTi);
    vector<Stack> srcRem;
    for (int i = 0; i < (int)g_load[srcTi].stacks.size(); i++)
        if (i != idx) srcRem.push_back(g_load[srcTi].stacks[i]);
    vector<Stack> srcOut;
    if (!tryLayoutWith(src, srcRem, srcOut)) return false;
    if (!tryAddStack(destTi, st)) return false;
    g_load[srcTi].stacks = std::move(srcOut);
    g_load[srcTi].used = !g_load[srcTi].stacks.empty();
    return true;
}

static void rollback(const vector<SavedLoad>& undo) {
    for (auto& u : undo) {
        g_load[u.ti].stacks = u.stacks;
        g_load[u.ti].used = u.used;
    }
}

// Try to empty a truck by moving all its stacks to other used trucks.
static bool eliminateTruck(int srcTi, double* deltaOut) {
    g_ctx = "eliminate";
    if (!g_load[srcTi].used || g_load[srcTi].stacks.empty()) return false;
    const Truck& srct = g_trucks[srcTi];
    vector<Stack> orig = g_load[srcTi].stacks;
    vector<SavedLoad> undo;
    double invDelta = 0;
    while (!g_load[srcTi].stacks.empty()) {
        int idx = 0;
        for (int i = 1; i < (int)g_load[srcTi].stacks.size(); i++)
            if (g_load[srcTi].stacks[i].wt > g_load[srcTi].stacks[idx].wt) idx = i;
        Stack st = g_load[srcTi].stacks[idx];
        vector<int> cands = g_items[st.line].cand;
        sort(cands.begin(), cands.end(), [](int a, int b) {
            bool ua = g_load[a].used, ub = g_load[b].used;
            if (ua != ub) return ua > ub;
            return g_trucks[a].day > g_trucks[b].day;
        });
        bool moved = false;
        for (int dest : cands) {
            if (dest == srcTi) continue;
            if (!g_load[dest].used) continue;
            if (!moveStackTo(srcTi, idx, dest, undo)) continue;
            for (auto& c : st.comps) invDelta += (double)g_items[c.line].ic * (double)(srct.day - g_trucks[dest].day) * c.qty;
            moved = true;
            break;
        }
        if (!moved) {
            rollback(undo);
            g_load[srcTi].stacks = orig;
            g_load[srcTi].used = true;
            return false;
        }
    }
    double delta = -P.aT * truckFixedCost(srct) + P.aI * invDelta;
    if (delta >= -1e-9) {
        rollback(undo);
        g_load[srcTi].stacks = orig;
        g_load[srcTi].used = true;
        return false;
    }
    g_load[srcTi].stacks.clear();
    g_load[srcTi].used = false;
    if (deltaOut) *deltaOut = delta;
    return true;
}

// Try to move stacks to later trucks (reduces inventory).
static int improveInventory(int maxMoves) {
    g_ctx = "inventory";
    int moves = 0;
    for (int ti = 0; ti < (int)g_trucks.size() && moves < maxMoves; ti++) {
        if (!g_load[ti].used || g_load[ti].stacks.empty()) continue;
        if (elapsed() > DEADLINE) break;
        int day = g_trucks[ti].day;
        for (int idx = (int)g_load[ti].stacks.size() - 1; idx >= 0; idx--) {
            if (idx >= (int)g_load[ti].stacks.size()) continue;
            Stack st = g_load[ti].stacks[idx];
            const ItemLine& it = g_items[st.line];
            vector<int> cands = it.cand;
            sort(cands.begin(), cands.end(), [](int a, int b) { return g_trucks[a].day > g_trucks[b].day; });
            for (int dest : cands) {
                if (dest == ti) continue;
                if (!g_load[dest].used) continue;
                if (g_trucks[dest].day <= day) continue;
                vector<SavedLoad> undo;
                if (!moveStackTo(ti, idx, dest, undo)) continue;
                moves++;
                break;
            }
        }
    }
    return moves;
}


// development self-check: verify every placed stack is compatible with its truck
static int validateSolution() {
    int bad = 0;
    for (int ti = 0; ti < (int)g_trucks.size(); ti++) {
        if (!g_load[ti].used) continue;
        const Truck& t = g_trucks[ti];
        for (auto& st : g_load[ti].stacks) {
            const ItemLine& it = g_items[st.line];
            bool ok = stackCompatible(st, t);
            if (!ok && bad < 10) {
                bool inbase = false;
                if (t.baseIdx >= 0) {
                    for (int l : g_trucks[t.baseIdx].lines) if (l == st.line) { inbase = true; break; }
                }
                logf("BADSTACK ti=%d id=%s extra=%d base=%d line=%d inbase=%d baseprods=%zu extraprods=%zu basearr=%lld extraarr=%lld sup=%s prod=%s pdock=%s sdock=%s win=[%lld,%lld]\n",
                     ti, t.id.c_str(), (int)t.extra, t.baseIdx, st.line, (int)inbase,
                     t.baseIdx >= 0 ? g_trucks[t.baseIdx].prods.size() : 0, t.prods.size(),
                     t.baseIdx >= 0 ? g_trucks[t.baseIdx].arr : 0, t.arr,
                     g_str[it.sup].c_str(), g_str[it.prod].c_str(),
                     g_str[it.pdock].c_str(), g_str[it.sdock].c_str(), it.t0, it.t1);
                bad++;
            }
        }
    }
    if (bad) logf("VALIDATE bad=%d\n", bad);
    return bad;
}


// Open an extra copy of a later-arriving truck when the inventory saving pays for it.
// After opening, fill the new truck with more compatible stacks from earlier trucks.
static int openLateTrucks(int maxMoves) {
    g_ctx = "late";
    int moves = 0;
    vector<int> order;
    for (int ti = 0; ti < (int)g_trucks.size(); ti++) {
        if (!g_load[ti].used || g_load[ti].stacks.empty()) continue;
        if (g_trucks[ti].extra) continue;
        order.push_back(ti);
    }
    sort(order.begin(), order.end(), [](int a, int b) { return g_trucks[a].day < g_trucks[b].day; });
    for (int ti : order) {
        if (elapsed() > g_lsDeadline) break;
        if (!g_load[ti].used || g_load[ti].stacks.empty()) continue;
        int day = g_trucks[ti].day;
        for (int idx = (int)g_load[ti].stacks.size() - 1; idx >= 0; idx--) {
            if (idx >= (int)g_load[ti].stacks.size()) continue;
            if (elapsed() > g_lsDeadline) break;
            Stack st = g_load[ti].stacks[idx];
            if (st.comps.empty()) continue;
            int best = -1, bestDay = day;
            for (int cand : g_items[st.comps[0].line].cand) {
                const Truck& t2 = g_trucks[cand];
                if (t2.day <= bestDay) continue;
                if (!stackCompatible(st, t2)) continue;
                best = cand; bestDay = t2.day;
            }
            if (best < 0) continue;
            double saved = 0;
            for (auto& c : st.comps) saved += (double)g_items[c.line].ic * (double)(bestDay - day) * c.qty;
            double gain = P.aI * saved - P.aT * ((double)g_trucks[best].cost * (1.0 + P.aE));
            if (gain <= 1e-9) continue;
            vector<Stack> srcRem;
            for (int i = 0; i < (int)g_load[ti].stacks.size(); i++) if (i != idx) srcRem.push_back(g_load[ti].stacks[i]);
            vector<Stack> srcOut;
            if (!tryLayoutWith(g_trucks[ti], srcRem, srcOut)) continue;
            Truck et = g_trucks[best];
            et.extra = true; et.baseIdx = best;
            et.id = "Q" + g_trucks[best].id.substr(1) + "_L" + to_string(++g_extraSeq);
            int ni = (int)g_trucks.size();
            g_trucks.push_back(et);
            g_load.emplace_back();
            if (!tryAddStack(ni, st)) { g_trucks.pop_back(); g_load.pop_back(); continue; }
            g_load[ti].stacks = std::move(srcOut);
            g_load[ti].used = !g_load[ti].stacks.empty();
            moves++;
            // fill the new truck from other earlier trucks
            int newDay = g_trucks[ni].day;
            for (int tj : order) {
                if (tj == ni || !g_load[tj].used) continue;
                if (g_trucks[tj].day >= newDay) continue;
                bool any = true;
                while (any && !g_load[tj].stacks.empty()) {
                    any = false;
                    for (int k = (int)g_load[tj].stacks.size() - 1; k >= 0; k--) {
                        if (k >= (int)g_load[tj].stacks.size()) continue;
                        Stack s2 = g_load[tj].stacks[k];
                        double sv = 0;
                        for (auto& c : s2.comps) sv += (double)g_items[c.line].ic * (double)(newDay - g_trucks[tj].day) * c.qty;
                        if (P.aI * sv <= 1e-9) continue;
                        vector<Stack> rem2;
                        for (int i = 0; i < (int)g_load[tj].stacks.size(); i++) if (i != k) rem2.push_back(g_load[tj].stacks[i]);
                        vector<Stack> out2;
                        if (!tryLayoutWith(g_trucks[tj], rem2, out2)) continue;
                        if (!tryAddStack(ni, s2)) continue;
                        g_load[tj].stacks = std::move(out2);
                        g_load[tj].used = !g_load[tj].stacks.empty();
                        any = true;
                        moves++;
                        break;
                    }
                }
                if (elapsed() > g_lsDeadline) break;
            }
        }
    }
    return moves;
}

static void localSearch() {
    if (elapsed() > DEADLINE - 2.0) return;
    double lsDeadline = min(DEADLINE - 2.0, elapsed() + LS_TIME_CAP);
    g_lsDeadline = lsDeadline;
    int totalElim = 0, totalInv = 0;
    for (int pass = 0; pass < 6; pass++) {
        if (elapsed() > lsDeadline) break;
        int invMoves = 0;
        vector<int> cand;
        for (int ti = 0; ti < (int)g_trucks.size(); ti++) {
            if (!g_load[ti].used || g_load[ti].stacks.empty()) continue;
            cand.push_back(ti);
        }
        sort(cand.begin(), cand.end(), [](int a, int b) {
            bool ea = g_trucks[a].extra, eb = g_trucks[b].extra;
            if (ea != eb) return ea > eb;
            double wa = 0, wb = 0;
            for (auto& s : g_load[a].stacks) wa += s.wt;
            for (auto& s : g_load[b].stacks) wb += s.wt;
            return wa < wb;
        });
        int elim = 0;
        for (int ti : cand) {
            if (elapsed() > lsDeadline) break;
            if (g_load[ti].used && !g_load[ti].stacks.empty()) {
                if (eliminateTruck(ti, nullptr)) elim++;
            }
        }
        totalElim += elim;
        int lateMoves = openLateTrucks(200000);
        totalElim += 0;
        logf("localSearch pass=%d lateMoves=%d t=%.1f\n", pass, lateMoves, elapsed());
        int invMoves2 = improveInventory(200000);
        totalInv += invMoves2;
        invMoves += invMoves2;
        logf("localSearch pass=%d invMoves=%d eliminated=%d t=%.1f\n", pass, invMoves, elim, elapsed());
        if (elim == 0 && invMoves == 0) break;
    }
    logf("localSearch done: invMoves=%d eliminated=%d t=%.1f\n", totalInv, totalElim, elapsed());
}


static void validateCounts() {
    vector<long long> cnt(g_items.size(), 0);
    for (int ti = 0; ti < (int)g_trucks.size(); ti++) {
        if (!g_load[ti].used) continue;
        for (auto& st : g_load[ti].stacks) {
            long long sum = 0;
            for (auto& c : st.comps) { cnt[c.line] += c.qty; sum += c.qty; }
            if (sum != st.qty) logf("BADQTY ti=%d stack qty=%d comps=%lld line=%d\n", ti, st.qty, sum, st.line);
        }
    }
    int bad = 0;
    for (int i = 0; i < (int)g_items.size(); i++) {
        if (cnt[i] != (long long)g_items[i].qty) {
            if (bad < 3) {
                logf("COUNT line=%d input=%d output=%lld rem=%d\n", i, g_items[i].qty, cnt[i], g_items[i].rem);
                for (int ti = 0; ti < (int)g_trucks.size(); ti++) {
                    if (!g_load[ti].used) continue;
                    for (size_t si = 0; si < g_load[ti].stacks.size(); si++) {
                        auto& st = g_load[ti].stacks[si];
                        long long q = 0;
                        for (auto& c : st.comps) if (c.line == i) q += c.qty;
                        if (q) logf("   in ti=%d id=%s stack=%zu compq=%lld comps=%zu qty=%d\n", ti, g_trucks[ti].id.c_str(), si, q, st.comps.size(), st.qty);
                    }
                }
            }
            bad++;
        }
    }
    logf("COUNTCHECK bad=%d\n", bad);
}

int main(int argc, char** argv) {
    T0 = Clock::now();
    if (argc < 5) { fprintf(stderr, "usage: solver in_dir out_dir internal_seconds seed\n"); return 2; }
    string inDir = argv[1], outDir = argv[2];
    double internal = atof(argv[3]);
    (void)argv[4];
    DEADLINE = internal - 5.0;
    if (DEADLINE < 1) DEADLINE = 1;
    logf("start in=%s internal=%.0f\n", inDir.c_str(), internal);
    parseParameters(inDir + "/input_parameters.csv");
    parseItems(inDir + "/input_items.csv");
    parseTrucks(inDir + "/input_trucks.csv");
    logf("parse done t=%.2f\n", elapsed());
    buildCandidates();
    logf("candidates done t=%.2f\n", elapsed());
    g_plannedTrucks = (int)g_trucks.size();
    {
        double areas[4] = { 0.95, 0.85, 0.75, 0.90 };
        int nvariants = 4;
        if (const char* e = getenv("DSH_SINGLE")) if (e[0] == '1') nvariants = 1;
        if (const char* e = getenv("DSH_FILLA")) areas[0] = atof(e);
        Snapshot best; double bestObj = 1e30; int bestVariant = -1;
        for (int v = 0; v < nvariants; v++) {
            if (elapsed() > DEADLINE - 30.0) break;
            resetState();
            double t0 = elapsed();
            construct(areas[v]);
            long long nt = 0; double inv = 0;
            double obj = computeObjective(&nt, &inv);
            logf("VARIANT v=%d fillArea=%.2f trucks=%lld inv=%.1f obj=%.1f dt=%.1f\n", v, areas[v], nt, inv, obj, elapsed() - t0);
            if (obj < bestObj) { bestObj = obj; best = snapshotSol(); bestVariant = v; }
        }
        if (bestVariant >= 0) {
            restoreSol(best);
            logf("VARIANT best=%d obj=%.1f t=%.1f\n", bestVariant, bestObj, elapsed());
        }
    }
    logf("after construct remaining=%lld t=%.1f\n", g_remaining, elapsed());
    localSearch();
    validateSolution();
    validateCounts();
    long long nt; double inv;
    double obj = computeObjective(&nt, &inv);
    logf("solution trucks=%lld inventory=%.1f objective=%.1f t=%.1f\n", nt, inv, obj, elapsed());
    writeOutput(outDir);
    logf("written t=%.2f\n", elapsed());
    return 0;
}
