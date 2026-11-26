// ROADEF2022 Truck Loading solver - from-scratch implementation (v014)
// v008 + per-thread simulated-annealing incumbents (per-key frontier reverted:
// it fragmented layouts and hurt consolidation).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include <set>
#include <random>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
using namespace std;
using Clock = chrono::steady_clock;

static Clock::time_point T0;
static double elapsed() { return chrono::duration<double>(Clock::now() - T0).count(); }
static double g_deadline = 1e18;
static bool timeLeft(double margin = 5.0) { return elapsed() < g_deadline - margin; }

static int days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
struct TimeStamp { int y, m, d, hh, mm; long long num; int day; };
static TimeStamp parseTS(const string &s) {
    TimeStamp t{};
    t.num = stoll(s);
    long long day = t.num / 10000;
    t.y = (int)(day / 10000); t.m = (int)(day / 100 % 100); t.d = (int)(day % 100);
    t.hh = (int)(t.num / 100 % 100); t.mm = (int)(t.num % 100);
    t.day = days_from_civil(t.y, t.m, t.d);
    return t;
}
static double parseNum(const string &s) {
    string t = s;
    for (auto &c : t) if (c == ',') c = '.';
    if (t.empty()) return 0.0;
    return stod(t);
}
struct StrInterner {
    unordered_map<string, int> m;
    vector<string> names;
    int intern(const string &s) {
        auto it = m.find(s);
        if (it != m.end()) return it->second;
        int id = (int)names.size(); names.push_back(s); m[s] = id; return id;
    }
    int size() const { return (int)names.size(); }
};

struct ItemGroup {
    string id;
    long long qty = 0;
    int supplier=-1, sdock=-1, plant=-1, pdock=-1, product=-1, code=-1;
    int L=0, W=0, H=0, nest=0, ISM=0, IC=0;
    double weight=0;
    int forcedOrient=0;
    long long earliest=0, latest=0;
    int latestDay=0;
    long long p4key=0;
};
struct TruckDef {
    string id;
    int plant=-1;
    long long arrival=0;
    int arrDay=0;
    int L=0, W=0, H=0, maxWeight=0, maxDensity=0;
    bool multiDock=false;
    long long cost=0;
    double EMmm=0, EMmr=0, CM=0, CJfm=0, CJfc=0, CJfh=0, EM=0, EJhr=0, EJcr=0, EJeh=0;
    unordered_map<int,int> supOrder, sdockOrder, pdockOrder;
    unordered_set<int> products;
    unordered_map<int,int> maxAbove;
};
struct Instance {
    vector<ItemGroup> groups;
    vector<TruckDef> trucks;
    double coeffInv=1, coeffTra=1, coeffExtra=0.2;
    StrInterner ids;
    unordered_map<int, vector<int>> supTrucks;
    vector<vector<int>> groupCands;
};

static vector<vector<string>> readCsv(const string &path) {
    ifstream f(path, ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    string data((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    vector<vector<string>> rows;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        string line = eol == string::npos ? data.substr(pos) : data.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol == string::npos ? data.size() : eol + 1;
        if (line.find(';') == string::npos) continue;
        vector<string> cells; size_t c = 0;
        while (true) {
            size_t s = line.find(';', c);
            if (s == string::npos) { cells.push_back(line.substr(c)); break; }
            cells.push_back(line.substr(c, s - c)); c = s + 1;
        }
        rows.push_back(move(cells));
    }
    return rows;
}

static bool unitFits(const ItemGroup &g, const TruckDef &t) {
    if (g.H > t.H) return false;
    auto fits = [&](int xdim, int ydim) { return xdim <= t.L && ydim <= t.W; };
    if (g.forcedOrient == 1) { if (!fits(g.L, g.W)) return false; }
    else if (g.forcedOrient == 2) { if (!fits(g.W, g.L)) return false; }
    else { if (!fits(g.L, g.W) && !fits(g.W, g.L)) return false; }
    if (g.weight > t.maxWeight) return false;
    double fp = (double)g.L * g.W / 1e6;
    if (fp > 0 && g.weight / fp >= t.maxDensity + 1.0) return false;
    return true;
}

static void buildCands(const Instance &inst, const ItemGroup &g, vector<int> &out) {
    auto it = inst.supTrucks.find(g.supplier);
    if (it == inst.supTrucks.end()) return;
    for (int ti : it->second) {
        const TruckDef &t = inst.trucks[ti];
        if (t.plant != g.plant) continue;
        if (!t.products.count(g.product)) continue;
        if (!t.sdockOrder.count(g.sdock)) continue;
        if (!t.pdockOrder.count(g.pdock)) continue;
        if (t.arrival < g.earliest || t.arrival > g.latest) continue;
        if (!unitFits(g, t)) continue;
        out.push_back(ti);
    }
}

static void parseInto(const string &dir, Instance &inst) {
    auto itemRows = readCsv(dir + "/input_items.csv");
    for (size_t i = 1; i < itemRows.size(); i++) {
        auto &r = itemRows[i];
        if (r.empty() || r[0].empty()) continue;
        ItemGroup g;
        g.id = r[0];
        g.supplier = inst.ids.intern(r[1]);
        g.sdock = inst.ids.intern(r[2]);
        g.plant = inst.ids.intern(r[3]);
        g.pdock = inst.ids.intern(r[4]);
        g.product = inst.ids.intern(r[5]);
        g.code = inst.ids.intern(r[13]);
        g.qty = stoll(r[7]);
        g.L = stoi(r[8]); g.W = stoi(r[9]); g.H = stoi(r[10]);
        g.weight = parseNum(r[11]);
        g.nest = stoi(r[12]);
        string fo = r[14]; for (auto &c : fo) c = (char)toupper(c);
        if (fo == "LENGTHWISE") g.forcedOrient = 1;
        else if (fo == "WIDTHWISE") g.forcedOrient = 2;
        g.earliest = stoll(r[15]); g.latest = stoll(r[16]);
        g.IC = stoi(r[17]);
        g.ISM = stoi(r[18]);
        g.latestDay = parseTS(r[16]).day;
        g.p4key = (((long long)g.supplier * 1000003LL + g.sdock) * 1000003LL + g.pdock) * 1000003LL + g.plant;
        inst.groups.push_back(move(g));
    }
    auto trRows = readCsv(dir + "/input_trucks.csv");
    unordered_map<string, int> tidIdx;
    for (size_t i = 1; i < trRows.size(); i++) {
        auto &r = trRows[i];
        if (r.empty() || r[0].empty()) continue;
        string ident = r[9];
        ident.erase(remove(ident.begin(), ident.end(), ' '), ident.end());
        int idx;
        auto it = tidIdx.find(ident);
        if (it == tidIdx.end()) {
            TruckDef t;
            t.id = ident;
            t.plant = inst.ids.intern(r[4]);
            t.arrival = stoll(r[8]);
            t.arrDay = parseTS(r[8]).day;
            t.L = stoi(r[10]); t.W = stoi(r[11]); t.H = stoi(r[12]);
            t.maxWeight = stoi(r[13]);
            t.multiDock = (r[14] == "1");
            t.maxDensity = stoi(r[15]);
            t.cost = stoll(r[17]);
            t.EMmm = parseNum(r[18]); t.EMmr = parseNum(r[19]); t.CM = parseNum(r[20]);
            t.CJfm = parseNum(r[21]); t.CJfc = parseNum(r[22]); t.CJfh = parseNum(r[23]);
            t.EM = parseNum(r[24]); t.EJhr = parseNum(r[25]); t.EJcr = parseNum(r[26]);
            t.EJeh = parseNum(r[27]);
            inst.trucks.push_back(move(t));
            idx = (int)inst.trucks.size() - 1;
            tidIdx[ident] = idx;
        } else idx = it->second;
        TruckDef &t = inst.trucks[idx];
        int supplier = inst.ids.intern(r[0]);
        t.supOrder[supplier] = stoi(r[1]);
        int sdock = inst.ids.intern(r[2]);
        t.sdockOrder[sdock] = stoi(r[3]);
        int pdock = inst.ids.intern(r[5]);
        t.pdockOrder[pdock] = stoi(r[6]);
        int product = inst.ids.intern(r[7]);
        t.products.insert(product);
        t.maxAbove[product] = stoi(r[16]);
    }
    auto parRows = readCsv(dir + "/input_parameters.csv");
    if (parRows.size() > 1) {
        inst.coeffInv = parseNum(parRows[1][0]);
        inst.coeffTra = parseNum(parRows[1][1]);
        inst.coeffExtra = parseNum(parRows[1][2]);
    }
    for (int i = 0; i < (int)inst.trucks.size(); i++)
        for (auto &kv : inst.trucks[i].supOrder)
            inst.supTrucks[kv.first].push_back(i);
    inst.groupCands.resize(inst.groups.size());
    for (size_t i = 0; i < inst.groups.size(); i++) {
        buildCands(inst, inst.groups[i], inst.groupCands[i]);
        if (inst.groupCands[i].empty()) {
            fprintf(stderr, "FATAL: item %s has no candidate truck\n", inst.groups[i].id.c_str());
            exit(3);
        }
    }
}

struct OutStack {
    int truckIdx = -1;
    int xO=0,yO=0,xE=0,yE=0,zE=0;
    double weight=0;
    int supplier=-1, sdock=-1, pdock=-1, code=-1, product=-1;
    int orient=0;
    long long p4key=0;
    vector<pair<int,long long>> units;
};
struct OutTruck {
    int defIdx=-1; bool extra=false; string outId;
    vector<int> stacks;
};
struct Solution {
    vector<OutTruck> trucks;
    vector<OutStack> stacks;
    double transport=0, inventory=0, objective=0;
    int nTrucksPlanned=0, nTrucksExtra=0;
    bool fatal=false;
};

struct UsedTruck {
    int defIdx=-1; bool extra=false; string outId;
    int extraSeq=0;
    bool dirty=true; // assignment changed since layout was built
    double weightSum=0, areaSum=0, floorArea=1;
    vector<pair<int,long long>> assign;
    unordered_map<long long, double> keyUnits;
};

static int estStackCap(const ItemGroup &g, const TruckDef &t) {
    int k = max(1, g.ISM);
    if (g.nest > 0 && g.H > g.nest) k = min(k, 1 + (t.H - g.H) / (g.H - g.nest));
    else k = min(k, t.H / max(1, g.H));
    auto ma = t.maxAbove.find(g.product);
    double lim = (ma == t.maxAbove.end()) ? 1e18 : ma->second;
    if (g.weight > 0) k = min(k, (int)floor(lim / g.weight) + 1);
    double fp = (double)g.L * g.W / 1e6;
    if (fp > 0 && g.weight > 0)
        k = min(k, (int)floor((t.maxDensity + 0.5) * fp / g.weight));
    return max(1, k);
}

struct AssignParams {
    double util = 0.92;
    bool affinity = true;
    int orderMode = 0;
    double affNoise = 0.0;
    double alpha = 0.3;  // marginal cost fraction of a new truck
    double beta = 0.10;  // affinity bonus (fraction of truck cost)
};

static vector<UsedTruck> assignGreedy(Instance &inst, const AssignParams &ap, mt19937 &rng) {
    int n = (int)inst.groups.size();
    vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    vector<int> nc(n);
    for (int i = 0; i < n; i++) nc[i] = (int)inst.groupCands[i].size();
    if (ap.orderMode == 0) {
        sort(order.begin(), order.end(), [&](int a, int b) {
            if (inst.groups[a].latestDay != inst.groups[b].latestDay)
                return inst.groups[a].latestDay < inst.groups[b].latestDay;
            if (nc[a] != nc[b]) return nc[a] < nc[b];
            return inst.groups[a].qty > inst.groups[b].qty;
        });
    } else if (ap.orderMode == 1) {
        sort(order.begin(), order.end(), [&](int a, int b) {
            if (nc[a] != nc[b]) return nc[a] < nc[b];
            if (inst.groups[a].latestDay != inst.groups[b].latestDay)
                return inst.groups[a].latestDay < inst.groups[b].latestDay;
            return inst.groups[a].qty > inst.groups[b].qty;
        });
    } else {
        sort(order.begin(), order.end(), [&](int a, int b) {
            double va = inst.groups[a].qty * inst.groups[a].weight;
            double vb = inst.groups[b].qty * inst.groups[b].weight;
            if (va != vb) return va > vb;
            return inst.groups[a].latestDay < inst.groups[b].latestDay;
        });
    }
    vector<UsedTruck> used;
    unordered_map<int,int> usedByDef;
    unordered_map<int,int> extraCount;
    unordered_map<int,int> lastExtraOf;
    uniform_real_distribution<double> uni(0.0, 1.0);
    for (int gi : order) {
        ItemGroup &g = inst.groups[gi];
        long long q = g.qty;
        vector<int> &cs = inst.groupCands[gi];
        vector<double> aff(cs.size(), 0.0);
        vector<int> early(cs.size(), 0), fillr(cs.size(), 0);
        vector<char> opened(cs.size(), 0);
        for (size_t j = 0; j < cs.size(); j++) {
            int ti = cs[j];
            early[j] = g.latestDay - inst.trucks[ti].arrDay;
            auto u = usedByDef.find(ti);
            if (u != usedByDef.end()) {
                opened[j] = 1;
                UsedTruck &ut = used[u->second];
                fillr[j] = (int)(1000.0 * ut.areaSum / ut.floorArea);
                auto ka = ut.keyUnits.find(g.p4key);
                if (ka != ut.keyUnits.end()) aff[j] = ka->second;
            }
        }
        vector<int> idx(cs.size());
        for (size_t j = 0; j < idx.size(); j++) idx[j] = (int)j;
        // precompute noisy affinity (comparator must be a pure function)
        vector<double> affN(cs.size(), 0.0);
        if (ap.affinity)
            for (size_t j = 0; j < cs.size(); j++) {
                affN[j] = aff[j];
                if (ap.affNoise > 0) affN[j] += uni(rng) * ap.affNoise;
            }
        // cost-unified score: marginal inventory vs marginal truck cost
        vector<double> score(cs.size(), 0.0);
        for (size_t j = 0; j < cs.size(); j++) {
            const TruckDef &t = inst.trucks[cs[j]];
            score[j] = (double)early[j] * g.IC * (double)q * inst.coeffInv
                     + (opened[j] ? 0.0 : ap.alpha * inst.coeffTra * (double)t.cost)
                     - ap.beta * inst.coeffTra * (double)t.cost * min(1.0, affN[j] / 50.0);
        }
        sort(idx.begin(), idx.end(), [&](int a, int b) {
            if (score[a] != score[b]) return score[a] < score[b];
            if (early[a] != early[b]) return early[a] < early[b];
            if (opened[a] != opened[b]) return opened[a] > opened[b];
            if (fillr[a] != fillr[b]) return fillr[a] > fillr[b];
            return cs[a] < cs[b];
        });
        int bestBase = cs[idx[0]];
        for (int j : idx) {
            if (q <= 0) break;
            int ti = cs[j];
            int uIdx;
            auto u = usedByDef.find(ti);
            if (u == usedByDef.end()) {
                UsedTruck ut; ut.defIdx = ti; ut.extra = false; ut.outId = inst.trucks[ti].id;
                ut.floorArea = (double)inst.trucks[ti].L * inst.trucks[ti].W;
                used.push_back(move(ut)); uIdx = (int)used.size() - 1;
                usedByDef[ti] = uIdx;
            } else uIdx = u->second;
            UsedTruck &ut = used[uIdx];
            const TruckDef &t = inst.trucks[ti];
            double cap = ut.floorArea * ap.util;
            double unitArea = (double)g.L * g.W / estStackCap(g, t);
            long long canArea = (long long)floor(max(0.0, (cap - ut.areaSum)) / max(unitArea, 1e-9));
            long long canW = (g.weight > 0) ? (long long)floor(max(0.0, (double)t.maxWeight - ut.weightSum) / g.weight) : (long long)1e18;
            long long take = min(q, min(canArea, canW));
            if (take <= 0) continue;
            ut.assign.push_back({gi, take});
            ut.areaSum += take * unitArea;
            ut.weightSum += take * g.weight;
            ut.keyUnits[g.p4key] += take;
            q -= take;
        }
        while (q > 0) {
            int ti = bestBase;
            const TruckDef &t = inst.trucks[ti];
            auto le = lastExtraOf.find(ti);
            if (le != lastExtraOf.end()) {
                UsedTruck &ut = used[le->second];
                double cap = ut.floorArea * ap.util;
                double unitArea = (double)g.L * g.W / estStackCap(g, t);
                long long canArea = (long long)floor(max(0.0, (cap - ut.areaSum)) / max(unitArea, 1e-9));
                long long canW = (g.weight > 0) ? (long long)floor(max(0.0, (double)t.maxWeight - ut.weightSum) / g.weight) : (long long)1e18;
                long long take = min(q, min(canArea, canW));
                if (take > 0) {
                    ut.assign.push_back({gi, take});
                    ut.areaSum += take * unitArea;
                    ut.weightSum += take * g.weight;
                    ut.keyUnits[g.p4key] += take;
                    q -= take;
                    continue;
                }
            }
            UsedTruck ut; ut.defIdx = ti; ut.extra = true;
            int seq = ++extraCount[ti];
            ut.outId = "Q" + inst.trucks[ti].id.substr(1) + "_" + to_string(seq);
            ut.extraSeq = seq;
            ut.floorArea = (double)t.L * t.W;
            double unitArea = (double)g.L * g.W / estStackCap(g, t);
            long long take = min(q, (long long)floor((ut.floorArea * ap.util) / max(unitArea, 1e-9)));
            if (g.weight > 0) take = min(take, (long long)floor((double)t.maxWeight / g.weight));
            take = max(take, 1LL);
            ut.assign.push_back({gi, take});
            ut.areaSum += take * unitArea;
            ut.weightSum += take * g.weight;
            ut.keyUnits[g.p4key] += take;
            q -= take;
            used.push_back(move(ut));
            lastExtraOf[ti] = (int)used.size() - 1;
        }
    }
    return used;
}

struct PackStackReq {
    int supplier=-1, sdock=-1, pdock=-1, code=-1, product=-1;
    int orient=0;
    int dx=0, wy=0, height=0;
    double weight=0;
    long long p4key=0;
    bool canFlip=false;
    vector<pair<int,long long>> units;
};

static void packTruckFloor(const TruckDef &t, vector<PackStackReq> &reqs, vector<OutStack> &placed, vector<PackStackReq> &spilled) {
    struct P { int xO, yO, xE, yE; };
    vector<P> ps;
    int frontier = 0; // max xOrigin over all placed stacks
    for (auto &r : reqs) {
        bool ok = false;
        // try original orientation, then flipped (if allowed)
        for (int flip = 0; flip < 2 && !ok; flip++) {
            PackStackReq rr = r;
            if (flip == 1) {
                if (!r.canFlip) break;
                swap(rr.dx, rr.wy);
                rr.orient = (rr.orient == 1) ? 2 : 1;
            }
            vector<int> xs;
            xs.push_back(frontier);
            for (auto &p : ps) if (p.xE > frontier) xs.push_back(p.xE);
            sort(xs.begin(), xs.end());
            xs.erase(unique(xs.begin(), xs.end()), xs.end());
            for (int x : xs) {
                if (x + rr.dx > t.L) continue;
                vector<int> ys;
                ys.push_back(0);
                for (auto &p : ps) {
                    if (p.xE == x) {
                        ys.push_back(p.yO);
                        if (p.yE - rr.wy >= 0) ys.push_back(p.yE - rr.wy);
                    }
                    ys.push_back(p.yO);
                    if (p.yE - rr.wy >= 0) ys.push_back(p.yE - rr.wy);
                    ys.push_back(p.yE);
                }
                sort(ys.begin(), ys.end());
                ys.erase(unique(ys.begin(), ys.end()), ys.end());
                for (int y : ys) {
                    if (y + rr.wy > t.W) continue;
                    if (x > 0) {
                        bool sup = false;
                        for (auto &p : ps)
                            if (p.xE == x && p.yO < y + rr.wy && p.yE > y) { sup = true; break; }
                        if (!sup) continue;
                    }
                    bool ov = false;
                    for (auto &p : ps)
                        if (p.xO < x + rr.dx && p.xE > x && p.yO < y + rr.wy && p.yE > y) { ov = true; break; }
                    if (ov) continue;
                    ps.push_back({x, y, x + rr.dx, y + rr.wy});
                    OutStack os;
                    os.truckIdx = -1;
                    os.xO = x; os.yO = y; os.xE = x + rr.dx; os.yE = y + rr.wy; os.zE = rr.height;
                    os.weight = rr.weight;
                    os.supplier = rr.supplier; os.sdock = rr.sdock; os.pdock = r.pdock;
                    os.code = rr.code; os.product = rr.product; os.orient = rr.orient;
                    os.p4key = rr.p4key;
                    os.units = rr.units;
                    placed.push_back(move(os));
                    frontier = max(frontier, x);
                    ok = true;
                    break;
                }
                if (ok) break;
            }
        }
        if (!ok) spilled.push_back(r);
    }
}

static bool axlePrefixOk(const TruckDef &t, const vector<OutStack> &st) {
    set<int> orders;
    for (auto &s : st) {
        auto o = t.supOrder.find(s.supplier);
        if (o == t.supOrder.end()) return false;
        orders.insert(o->second);
    }
    for (int mo : orders) {
        double tm = 0, mom = 0;
        for (auto &s : st) {
            int so = t.supOrder.at(s.supplier);
            if (so > mo) continue;
            tm += s.weight; mom += (s.xO + (s.xE - s.xO) / 2.0) * s.weight;
        }
        if (tm <= 0) continue;
        double eje = mom / tm;
        double ejr = t.EJeh + t.EJhr - eje;
        double emh = (tm * ejr + t.EM * t.EJcr) / t.EJhr;
        double emr = tm + t.EM - emh;
        double emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
        if (emm > t.EMmm + 0.1) return false;
        if (emr > t.EMmr + 0.1) return false;
    }
    return true;
}

static PackStackReq reqFromStack(const OutStack &vs) {
    PackStackReq r{};
    r.supplier = vs.supplier; r.sdock = vs.sdock; r.pdock = vs.pdock;
    r.code = vs.code; r.product = vs.product; r.orient = vs.orient;
    r.dx = vs.xE - vs.xO; r.wy = vs.yE - vs.yO; r.height = vs.zE; r.weight = vs.weight;
    r.p4key = vs.p4key;
    r.units = vs.units;
    return r;
}

static void packAndRepairTruck(const TruckDef &t, vector<PackStackReq> &reqs, vector<OutStack> &placed, vector<PackStackReq> &spilled) {
    placed.clear();
    vector<PackStackReq> still;
    packTruckFloor(t, reqs, placed, still);
    reqs.clear();
    for (auto &s : still) spilled.push_back(move(s));
    int guard = 0;
    while (!axlePrefixOk(t, placed) && !placed.empty() && guard++ < 10000) {
        double tm = 0, mom = 0;
        for (auto &s : placed) { tm += s.weight; mom += (s.xO + (s.xE - s.xO) / 2.0) * s.weight; }
        double eje = tm > 0 ? mom / tm : 0;
        double ejr = t.EJeh + t.EJhr - eje;
        double emh = tm > 0 ? (tm * ejr + t.EM * t.EJcr) / t.EJhr : 0;
        double emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
        int victim = -1;
        if (emm > t.EMmm + 0.1) {
            int bx = 1 << 30;
            for (size_t i = 0; i < placed.size(); i++) if (placed[i].xO < bx) { bx = placed[i].xO; victim = (int)i; }
        } else {
            int bx = -1;
            for (size_t i = 0; i < placed.size(); i++) if (placed[i].xO > bx) { bx = placed[i].xO; victim = (int)i; }
        }
        if (victim < 0) break;
        spilled.push_back(reqFromStack(placed[victim]));
        placed.erase(placed.begin() + victim);
        vector<PackStackReq> rest;
        for (auto &s : placed) rest.push_back(reqFromStack(s));
        placed.clear();
        vector<PackStackReq> still2;
        packTruckFloor(t, rest, placed, still2);
        for (auto &s : still2) spilled.push_back(move(s));
    }
}

static void buildStacksForTruck(const TruckDef &t, const Instance &inst,
                                const vector<pair<int,long long>> &assignIn,
                                vector<PackStackReq> &reqs) {
    struct Key { int a,b,c,d,e,f; bool operator==(const Key&k) const {return a==k.a&&b==k.b&&c==k.c&&d==k.d&&e==k.e&&f==k.f;} };
    struct KHash { size_t operator()(const Key&k) const { return hash<long long>()((((((long long)k.a*131+k.b)*131+k.c)*131+k.d)*131+k.e)*131+k.f); } };
    unordered_map<Key, vector<pair<int,long long>>, KHash> buckets;
    // first bucket sans orientation, then split by forced-majority class so that
    // unforced groups can still stack with forced ones (S4-safe)
    struct Key0 { int a,b,c,d,e; bool operator==(const Key0&k) const {return a==k.a&&b==k.b&&c==k.c&&d==k.d&&e==k.e;} };
    struct KHash0 { size_t operator()(const Key0&k) const { return hash<long long>()(((((long long)k.a*131+k.b)*131+k.c)*131+k.d)*131+k.e); } };
    unordered_map<Key0, vector<pair<int,long long>>, KHash0> buckets0;
    for (auto &pr : assignIn) {
        const ItemGroup &g = inst.groups[pr.first];
        Key0 k{g.supplier, g.sdock, g.pdock, g.code, g.product};
        buckets0[k].push_back(pr);
    }
    for (auto &kv : buckets0) {
        double w1 = 0, w2 = 0;
        for (auto &pr : kv.second) {
            const ItemGroup &g = inst.groups[pr.first];
            if (g.forcedOrient == 1) w1 += g.qty;
            else if (g.forcedOrient == 2) w2 += g.qty;
        }
        int domClass = (w1 >= w2) ? 1 : 2;
        bool hasForced = (w1 > 0 || w2 > 0);
        for (auto &pr : kv.second) {
            const ItemGroup &g = inst.groups[pr.first];
            int cls = 0;
            if (g.forcedOrient == 1) cls = 1;
            else if (g.forcedOrient == 2) cls = 2;
            // minority forced groups go to their own bucket; others join domClass bucket
            int bcls = hasForced ? domClass : 0;
            if (hasForced && cls != 0 && cls != domClass) bcls = cls;
            Key k{kv.first.a, kv.first.b, kv.first.c, kv.first.d, kv.first.e, bcls};
            buckets[k].push_back(pr);
        }
    }
    double truckW = 0;
    for (auto &kv : buckets) {
        const ItemGroup &g0 = inst.groups[kv.second[0].first];
        int orient = kv.first.f;
        if (!orient) {
            int lanesLen = t.W / g0.W;
            int lanesWid = t.W / g0.L;
            orient = (lanesWid > lanesLen) ? 2 : 1;
        }
        bool canFlip = (kv.first.f == 0);
        long long p4k = g0.p4key;
        map<int, vector<pair<int,long long>>> byISM;
        for (auto &pr : kv.second) byISM[inst.groups[pr.first].ISM].push_back(pr);
        for (auto &ism : byISM) {
            vector<pair<int,long long>> units = ism.second;
            sort(units.begin(), units.end(), [&](auto &a, auto &b) {
                return inst.groups[a.first].weight > inst.groups[b.first].weight;
            });
            auto ma = t.maxAbove.find(kv.first.e);
            double maxAbove = (ma == t.maxAbove.end()) ? 1e18 : ma->second;
            size_t ui = 0;
            while (true) {
                while (ui < units.size() && units[ui].second <= 0) ui++;
                if (ui >= units.size()) break;
                PackStackReq r{};
                r.supplier = kv.first.a; r.sdock = kv.first.b; r.pdock = kv.first.c;
                r.code = kv.first.d; r.product = kv.first.e; r.orient = orient;
                r.p4key = p4k;
                r.canFlip = canFlip;
                const ItemGroup *bg = &inst.groups[units[ui].first];
                r.dx = (orient == 1) ? bg->L : bg->W;
                r.wy = (orient == 1) ? bg->W : bg->L;
                r.height = bg->H;
                r.weight = bg->weight;
                r.units.push_back({units[ui].first, 1});
                units[ui].second--;
                double above = 0;
                int cnt = 1;
                bool progress = true;
                while (cnt < ism.first && progress) {
                    progress = false;
                    for (size_t j = units.size(); j-- > 0; ) {
                        if (units[j].second <= 0) continue;
                        const ItemGroup &g2 = inst.groups[units[j].first];
                        int newH = r.height + g2.H - g2.nest;
                        if (newH > t.H) continue;
                        if (above + g2.weight > maxAbove - 1e-9) continue;
                        double newW = r.weight + g2.weight;
                        if (newW > t.maxWeight - truckW) continue;
                        double dens = newW / ((double)r.dx * r.wy / 1e6);
                        if (floor(dens) > t.maxDensity) continue;
                        r.height = newH; r.weight = newW; above += g2.weight; cnt++;
                        if (!r.units.empty() && r.units.back().first == (int)units[j].first) r.units.back().second++;
                        else r.units.push_back({units[j].first, 1});
                        units[j].second--;
                        progress = true;
                        break;
                    }
                }
                truckW += r.weight;
                reqs.push_back(move(r));
            }
        }
    }
}

static void p4sort(const TruckDef &t, vector<PackStackReq> &reqs) {
    auto so = [&](int s) { auto it = t.supOrder.find(s); return it == t.supOrder.end() ? 1 << 20 : it->second; };
    auto sdo = [&](int s) { auto it = t.sdockOrder.find(s); return it == t.sdockOrder.end() ? 1 << 20 : it->second; };
    auto pdo = [&](int s) { auto it = t.pdockOrder.find(s); return it == t.pdockOrder.end() ? 1 << 20 : it->second; };
    sort(reqs.begin(), reqs.end(), [&](const PackStackReq &a, const PackStackReq &b) {
        int ka = so(a.supplier), kb = so(b.supplier);
        if (ka != kb) return ka < kb;
        ka = sdo(a.sdock); kb = sdo(b.sdock);
        if (ka != kb) return ka < kb;
        ka = pdo(a.pdock); kb = pdo(b.pdock);
        if (ka != kb) return ka < kb;
        if (a.dx != b.dx) return a.dx > b.dx;
        if (a.wy != b.wy) return a.wy > b.wy;
        return a.weight > b.weight;
    });
}

static bool splitReq(const Instance &inst, PackStackReq &r, PackStackReq &out2) {
    long long total = 0;
    for (auto &u : r.units) total += u.second;
    if (total < 2) return false;
    vector<pair<int,long long>> list;
    for (auto &u : r.units)
        for (long long c = 0; c < u.second; c++) list.push_back({u.first, 1});
    size_t half = list.size() / 2;
    auto rebuild = [&](size_t from, size_t to, PackStackReq &out) {
        out = PackStackReq{};
        out.supplier = r.supplier; out.sdock = r.sdock; out.pdock = r.pdock;
        out.code = r.code; out.product = r.product; out.orient = r.orient;
        out.dx = r.dx; out.wy = r.wy; out.p4key = r.p4key;
        int h = 0; double w = 0;
        for (size_t i = from; i < to; i++) {
            const ItemGroup &g = inst.groups[list[i].first];
            h = (i == from) ? g.H : h + g.H - g.nest;
            w += g.weight;
            if (!out.units.empty() && out.units.back().first == list[i].first) out.units.back().second++;
            else out.units.push_back({list[i].first, 1});
        }
        out.height = h; out.weight = w;
    };
    rebuild(0, half, r);
    rebuild(half, list.size(), out2);
    return true;
}

// P4 order comparison between a candidate req and an existing stack on truck t.
// Returns -1 (req before s), +1 (req after s), 0 (no constraint).
static int p4cmp(const TruckDef &t, const PackStackReq &r, const OutStack &s) {
    int so1 = t.supOrder.at(r.supplier), so2 = t.supOrder.at(s.supplier);
    if (so1 != so2) return so1 < so2 ? -1 : 1;
    int sd1 = t.sdockOrder.at(r.sdock), sd2 = t.sdockOrder.at(s.sdock);
    if (sd1 != sd2) return sd1 < sd2 ? -1 : 1;
    int pd1 = t.pdockOrder.at(r.pdock), pd2 = t.pdockOrder.at(s.pdock);
    if (pd1 == pd2) return 0;
    return pd1 < pd2 ? -1 : 1;
}

// try to insert one stack into a packed truck respecting P4 window, P2, P3, W1, W2, item-truck compat
static bool tryInsertStack(const TruckDef &t, const Instance &inst, vector<OutStack> &st, const PackStackReq &r) {
    if (!t.supOrder.count(r.supplier) || !t.sdockOrder.count(r.sdock) || !t.pdockOrder.count(r.pdock)) return false;
    // item compatibility: products + time windows + physical
    double dens = r.weight / ((double)r.dx * r.wy / 1e6);
    for (auto &u : r.units) {
        const ItemGroup &g = inst.groups[u.first];
        if (g.plant != t.plant) return false;
        if (!t.products.count(g.product)) return false;
        if (t.arrival < g.earliest || t.arrival > g.latest) return false;
    }
    if (r.dx > t.L || r.wy > t.W || r.height > t.H) return false;
    if (floor(dens) > t.maxDensity) return false;
    auto ma = t.maxAbove.find(r.product);
    if (ma != t.maxAbove.end()) {
        double above = r.weight;
        if (!r.units.empty()) {
            above = 0;
            for (size_t i = 0; i < r.units.size(); i++) {
                if (i == 0) above += inst.groups[r.units[i].first].weight * (r.units[i].second - 1);
                else above += inst.groups[r.units[i].first].weight * r.units[i].second;
            }
        }
        if (above > ma->second) return false;
    }
    double curW = 0;
    for (auto &s : st) curW += s.weight;
    if (curW + r.weight > t.maxWeight + 0.1) return false;
    int lo = 0, hi = t.L;
    for (auto &s : st) {
        int c = p4cmp(t, r, s);
        if (c > 0) lo = max(lo, s.xO);  // req after s: x >= s.xO
        else if (c < 0) hi = min(hi, s.xO); // req before s: x <= s.xO
    }
    if (lo > hi) return false;
    vector<int> xs;
    xs.push_back(lo);
    for (auto &s : st) {
        if (s.xE >= lo && s.xE <= hi) xs.push_back(s.xE);
        if (s.xO >= lo && s.xO <= hi) xs.push_back(s.xO);
    }
    sort(xs.begin(), xs.end());
    xs.erase(unique(xs.begin(), xs.end()), xs.end());
    for (int x : xs) {
        if (x + r.dx > t.L) continue;
        if (x > hi) break;
        vector<int> ys;
        ys.push_back(0);
        for (auto &s : st) {
            if (s.xE == x) { ys.push_back(s.yO); if (s.yE - r.wy >= 0) ys.push_back(s.yE - r.wy); }
            ys.push_back(s.yO);
            if (s.yE - r.wy >= 0) ys.push_back(s.yE - r.wy);
            ys.push_back(s.yE);
        }
        sort(ys.begin(), ys.end());
        ys.erase(unique(ys.begin(), ys.end()), ys.end());
        for (int y : ys) {
            if (y + r.wy > t.W) continue;
            if (x > 0) {
                bool sup = false;
                for (auto &s : st)
                    if (s.xE == x && s.yO < y + r.wy && s.yE > y) { sup = true; break; }
                if (!sup) continue;
            }
            bool ov = false;
            for (auto &s : st)
                if (s.xO < x + r.dx && s.xE > x && s.yO < y + r.wy && s.yE > y) { ov = true; break; }
            if (ov) continue;
            OutStack ns;
            ns.supplier = r.supplier; ns.sdock = r.sdock; ns.pdock = r.pdock;
            ns.code = r.code; ns.product = r.product; ns.orient = r.orient;
            ns.zE = r.height; ns.weight = r.weight; ns.p4key = r.p4key;
            ns.units = r.units;
            ns.truckIdx = -1;
            ns.xO = x; ns.yO = y; ns.xE = x + r.dx; ns.yE = y + r.wy;
            st.push_back(move(ns));
            if (axlePrefixOk(t, st)) return true;
            st.pop_back();
        }
    }
    return false;
}

static void recomputeCosts(Instance &inst, Solution &sol) {
    double transp = 0, inv = 0;
    sol.nTrucksPlanned = sol.nTrucksExtra = 0;
    for (auto &ot : sol.trucks) {
        const TruckDef &t = inst.trucks[ot.defIdx];
        transp += ot.extra ? t.cost * (1.0 + inst.coeffExtra) : (double)t.cost;
        if (ot.extra) sol.nTrucksExtra++; else sol.nTrucksPlanned++;
        for (int si : ot.stacks)
            for (auto &u : sol.stacks[si].units) {
                const ItemGroup &g = inst.groups[u.first];
                int dd = g.latestDay - t.arrDay;
                if (dd > 0) inv += (double)g.IC * dd * u.second;
            }
    }
    sol.transport = transp; sol.inventory = inv;
    sol.objective = inst.coeffTra * transp + inst.coeffInv * inv;
}

static Solution buildSolution(Instance &inst, vector<UsedTruck> &used, FILE *log, bool quiet,
                              const vector<vector<OutStack>> *cache = nullptr) {
    Solution sol;
    struct Spill { int baseDef; PackStackReq req; };
    vector<Spill> spillPool;
    vector<OutStack> allStacks;
    vector<OutTruck> outTrucks;
    unordered_map<int,int> extraCounter;
    for (auto &u : used) if (u.extra)
        extraCounter[u.defIdx] = max(extraCounter[u.defIdx], u.extraSeq);
    for (size_t ui = 0; ui < used.size(); ui++) {
        auto &u = used[ui];
        const TruckDef &t = inst.trucks[u.defIdx];
        vector<OutStack> placed;
        vector<PackStackReq> spilled;
        if (cache && !u.dirty && ui < cache->size()) {
            placed = (*cache)[ui]; // reuse existing layout (assignment unchanged)
        } else {
            vector<PackStackReq> reqs;
            buildStacksForTruck(t, inst, u.assign, reqs);
            p4sort(t, reqs);
            packAndRepairTruck(t, reqs, placed, spilled);
        }
        if (placed.empty()) continue; // all spilled; do not emit empty truck
        int truckIdx = (int)outTrucks.size();
        OutTruck ot;
        ot.defIdx = u.defIdx; ot.extra = u.extra; ot.outId = u.outId;
        int base = (int)allStacks.size();
        for (auto &s : placed) { s.truckIdx = truckIdx; allStacks.push_back(move(s)); ot.stacks.push_back(base++); }
        outTrucks.push_back(move(ot));
        for (auto &s : spilled) spillPool.push_back({u.defIdx, move(s)});
    }
    // build-time spill reinsertion into already-built trucks (before opening extras)
    if (!spillPool.empty()) {
        vector<vector<OutStack>> tstacks(outTrucks.size());
        for (size_t j = 0; j < outTrucks.size(); j++)
            for (int si : outTrucks[j].stacks) tstacks[j].push_back(allStacks[si]);
        sort(spillPool.begin(), spillPool.end(), [](const Spill &a, const Spill &b) {
            return a.req.dx * a.req.wy > b.req.dx * b.req.wy;
        });
        // target order: latest arrival first
        vector<int> torder;
        for (size_t j = 0; j < outTrucks.size(); j++) torder.push_back((int)j);
        sort(torder.begin(), torder.end(), [&](int a, int b) {
            return inst.trucks[outTrucks[a].defIdx].arrDay > inst.trucks[outTrucks[b].defIdx].arrDay;
        });
        vector<Spill> remain;
        int insertBudget = 4000000;
        for (auto &sp : spillPool) {
            if (insertBudget <= 0) { remain.push_back(move(sp)); continue; }
            const TruckDef &bt = inst.trucks[sp.baseDef];
            bool placed = false;
            for (int j : torder) {
                const TruckDef &tt = inst.trucks[outTrucks[j].defIdx];
                if (tt.plant != bt.plant) continue;
                if (tt.arrDay < bt.arrDay) continue; // only same-or-later arrivals: never add inventory
                if (tstacks[j].size() >= 400) continue;
                insertBudget -= (int)tstacks[j].size() + 8;
                if (tryInsertStack(tt, inst, tstacks[j], sp.req)) {
                    placed = true;
                    break;
                }
            }
            if (!placed) remain.push_back(move(sp));
        }
        // commit mutated layouts
        vector<OutTruck> ntr;
        vector<OutStack> nst;
        for (size_t j = 0; j < outTrucks.size(); j++) {
            if (tstacks[j].empty()) continue;
            OutTruck ot2 = move(outTrucks[j]);
            ot2.stacks.clear();
            int ti = (int)ntr.size();
            for (auto &s : tstacks[j]) {
                s.truckIdx = ti;
                ot2.stacks.push_back((int)nst.size());
                nst.push_back(move(s));
            }
            ntr.push_back(move(ot2));
        }
        outTrucks = move(ntr);
        allStacks = move(nst);
        spillPool = move(remain);
    }
    int guard = 0;
    while (!spillPool.empty() && guard++ < 200000) {
        unordered_map<int, vector<PackStackReq>> byBase;
        for (auto &s : spillPool) byBase[s.baseDef].push_back(move(s.req));
        spillPool.clear();
        bool fatal = false;
        for (auto &kv : byBase) {
            const TruckDef &t = inst.trucks[kv.first];
            vector<PackStackReq> reqs = move(kv.second);
            p4sort(t, reqs);
            vector<vector<PackStackReq>> chunks;
            {
                vector<PackStackReq> cur; double w = 0;
                for (auto &r : reqs) {
                    if (!cur.empty() && w + r.weight > t.maxWeight) { chunks.push_back(move(cur)); cur.clear(); w = 0; }
                    cur.push_back(move(r)); w += r.weight;
                }
                if (!cur.empty()) chunks.push_back(move(cur));
            }
            for (auto &chunk : chunks) {
                if (chunk.empty()) continue;
                vector<OutStack> placed;
                vector<PackStackReq> still;
                packAndRepairTruck(t, chunk, placed, still);
                if (placed.empty()) {
                    vector<PackStackReq> split;
                    bool any = false;
                    for (auto &r : still) {
                        PackStackReq half2;
                        if (splitReq(inst, r, half2)) { split.push_back(move(r)); split.push_back(move(half2)); any = true; }
                        else split.push_back(move(r));
                    }
                    if (any) { for (auto &r : split) spillPool.push_back({kv.first, move(r)}); continue; }
                    fatal = true;
                    continue;
                }
                int seq = ++extraCounter[kv.first];
                OutTruck ot;
                ot.defIdx = kv.first; ot.extra = true;
                ot.outId = "Q" + t.id.substr(1) + "_" + to_string(seq);
                int truckIdx = (int)outTrucks.size();
                int base = (int)allStacks.size();
                for (auto &s : placed) { s.truckIdx = truckIdx; allStacks.push_back(move(s)); ot.stacks.push_back(base++); }
                outTrucks.push_back(move(ot));
                for (auto &r : still) spillPool.push_back({kv.first, move(r)});
            }
        }
        if (fatal) { sol.fatal = true; return sol; }
    }
    if (!spillPool.empty()) { sol.fatal = true; return sol; }
    sol.trucks = move(outTrucks);
    sol.stacks = move(allStacks);
    recomputeCosts(inst, sol);
    if (!quiet)
        fprintf(log, "[solver] build: planned=%d extra=%d transport=%.0f inv=%.0f obj=%.0f stacks=%zu (%.1fs)\n",
                sol.nTrucksPlanned, sol.nTrucksExtra, sol.transport, sol.inventory, sol.objective, sol.stacks.size(), elapsed());
    return sol;
}

// consolidation: evacuate stacks of lightly-loaded trucks into others; drop emptied trucks
static void consolidate(Instance &inst, Solution &sol, FILE *log, int maxRounds) {
    for (int round = 0; round < maxRounds; round++) {
        if (!timeLeft(20.0)) break;
        vector<vector<OutStack>> cur(sol.trucks.size());
        for (size_t j = 0; j < sol.trucks.size(); j++)
            for (int si : sol.trucks[j].stacks) cur[j].push_back(sol.stacks[si]);
        vector<char> dead(sol.trucks.size(), 0);
        vector<int> victims;
        for (size_t i = 0; i < sol.trucks.size(); i++) victims.push_back((int)i);
        sort(victims.begin(), victims.end(), [&](int a, int b) {
            const OutTruck &A = sol.trucks[a], &B = sol.trucks[b];
            double wa = 0, wb = 0;
            for (auto &s : cur[a]) wa += s.weight;
            for (auto &s : cur[b]) wb += s.weight;
            if (A.extra != B.extra) return A.extra > B.extra;
            return wa < wb;
        });
        bool any = false;
        size_t tried = 0;
        for (int vi : victims) {
            if (tried++ > sol.trucks.size() / 2 + 4) break;
            if (!timeLeft(20.0)) break;
            if (dead[vi] || cur[vi].empty()) continue;
            const TruckDef &vtd = inst.trucks[sol.trucks[vi].defIdx];
            vector<OutStack> rem = move(cur[vi]);
            cur[vi].clear();
            sort(rem.begin(), rem.end(), [](const OutStack &a, const OutStack &b) {
                int aa = (a.xE - a.xO) * (a.yE - a.yO), bb = (b.xE - b.xO) * (b.yE - b.yO);
                return aa > bb;
            });
            // target order: latest arrival first (minimize added inventory days)
            vector<int> torder;
            for (size_t j = 0; j < sol.trucks.size(); j++) torder.push_back((int)j);
            sort(torder.begin(), torder.end(), [&](int a, int b) {
                return inst.trucks[sol.trucks[a].defIdx].arrDay > inst.trucks[sol.trucks[b].defIdx].arrDay;
            });
            vector<pair<int, vector<OutStack>>> backup; // modified target columns
            vector<OutStack> left;                       // unplaceable (original coords)
            vector<OutStack> placedOrig;                 // originals of inserted (for rollback)
            double deltaInv = 0;                         // inventory days cost added by moves
            for (auto &s : rem) {
                PackStackReq r = reqFromStack(s);
                bool placed = false;
                for (int j : torder) {
                    if (j == vi || dead[j]) continue;
                    if (cur[j].size() >= 400) continue;
                    bool known = false;
                    for (auto &b : backup) if (b.first == j) { known = true; break; }
                    if (!known) backup.push_back({j, cur[j]});
                    const TruckDef &tt = inst.trucks[sol.trucks[j].defIdx];
                    if (tryInsertStack(tt, inst, cur[j], r)) {
                        double di = 0;
                        for (auto &u : r.units) {
                            const ItemGroup &g = inst.groups[u.first];
                            di += (double)g.IC * (tt.arrDay - vtd.arrDay) * u.second;
                        }
                        deltaInv += di;
                        placed = true;
                        break;
                    }
                }
                if (placed) placedOrig.push_back(move(s));
                else left.push_back(move(s));
            }
            double truckSaving = (sol.trucks[vi].extra ? vtd.cost * (1.0 + inst.coeffExtra) : (double)vtd.cost) * inst.coeffTra;
            if (left.empty() && deltaInv * inst.coeffInv < truckSaving * 0.999) {
                dead[vi] = 1; // commit: inserted copies live in targets
                any = true;
            } else {
                // rollback: restore target columns, victim keeps its exact original layout
                for (auto &b : backup) cur[b.first] = move(b.second);
                cur[vi] = move(left);
                for (auto &s : placedOrig) cur[vi].push_back(move(s));
            }
        }
        if (!any) break;
        vector<OutTruck> ntr;
        vector<OutStack> nst;
        for (size_t j = 0; j < sol.trucks.size(); j++) {
            if (dead[j] || cur[j].empty()) continue;
            OutTruck ot = move(sol.trucks[j]);
            ot.stacks.clear();
            int ti = (int)ntr.size();
            for (auto &s : cur[j]) {
                s.truckIdx = ti;
                ot.stacks.push_back((int)nst.size());
                nst.push_back(move(s));
            }
            ntr.push_back(move(ot));
        }
        sol.trucks = move(ntr);
        sol.stacks = move(nst);
        recomputeCosts(inst, sol);
    }
}

// ---- LNS machinery ----
static int parseExtraSeq(const string &id) {
    auto p = id.rfind('_');
    if (p == string::npos) return 0;
    return atoi(id.c_str() + p + 1);
}

// convert a packed solution back into assignment form with occupied-capacity state
static vector<UsedTruck> solutionToAssignment(Instance &inst, Solution &sol, vector<vector<OutStack>> *layouts = nullptr) {
    vector<UsedTruck> used;
    for (auto &ot : sol.trucks) {
        UsedTruck u;
        u.defIdx = ot.defIdx; u.extra = ot.extra; u.outId = ot.outId;
        u.extraSeq = ot.extra ? parseExtraSeq(ot.outId) : 0;
        u.dirty = false; // layout exists in the solution
        const TruckDef &t = inst.trucks[ot.defIdx];
        u.floorArea = (double)t.L * t.W;
        unordered_map<int, long long> agg;
        for (int si : ot.stacks)
            for (auto &pr : sol.stacks[si].units) agg[pr.first] += pr.second;
        for (auto &kv : agg) {
            u.assign.push_back({kv.first, kv.second});
            const ItemGroup &g = inst.groups[kv.first];
            u.weightSum += g.weight * kv.second;
            u.areaSum += (double)g.L * g.W / estStackCap(g, t) * kv.second;
            u.keyUnits[g.p4key] += (double)kv.second;
        }
        if (layouts) {
            vector<OutStack> lay;
            for (int si : ot.stacks) lay.push_back(sol.stacks[si]);
            layouts->push_back(move(lay));
        }
        used.push_back(move(u));
    }
    return used;
}

// refill pool items into existing used trucks, opening new trucks when needed
static void assignRefill(Instance &inst, vector<UsedTruck> &used, vector<pair<int,long long>> &pool,
                         const AssignParams &ap, mt19937 &rng, double utilRefill) {
    // per-def lists of existing trucks
    unordered_map<int, vector<int>> byDef;
    unordered_map<int,int> extraCount;
    for (size_t i = 0; i < used.size(); i++) {
        byDef[used[i].defIdx].push_back((int)i);
        if (used[i].extra) extraCount[used[i].defIdx] = max(extraCount[used[i].defIdx], used[i].extraSeq);
    }
    sort(pool.begin(), pool.end(), [&](auto &a, auto &b) {
        const ItemGroup &ga = inst.groups[a.first], &gb = inst.groups[b.first];
        if (ga.latestDay != gb.latestDay) return ga.latestDay < gb.latestDay;
        int ca = (int)inst.groupCands[a.first].size(), cb = (int)inst.groupCands[b.first].size();
        if (ca != cb) return ca < cb;
        return a.second > b.second;
    });
    uniform_real_distribution<double> uni(0.0, 1.0);
    for (auto &pr : pool) {
        int gi = pr.first;
        ItemGroup &g = inst.groups[gi];
        long long q = pr.second;
        vector<int> &cs = inst.groupCands[gi];
        struct Cand { int usedIdx; int early; int fillr; double aff; double cost; };
        vector<Cand> cands;
        for (int ti : cs) {
            const TruckDef &t = inst.trucks[ti];
            int early = g.latestDay - t.arrDay;
            auto it = byDef.find(ti);
            if (it != byDef.end()) {
                for (int ui2 : it->second) {
                    UsedTruck &ut = used[ui2];
                    Cand c;
                    c.usedIdx = ui2; c.early = early;
                    c.fillr = (int)(1000.0 * ut.areaSum / (ut.floorArea * utilRefill));
                    auto ka = ut.keyUnits.find(g.p4key);
                    c.aff = (ka == ut.keyUnits.end()) ? 0.0 : ka->second;
                    c.cost = (double)early * g.IC * (double)q * inst.coeffInv - ap.beta * inst.coeffTra * (double)t.cost * min(1.0, c.aff / 50.0);
                    cands.push_back(c);
                }
            }
            // potential new planned truck
            Cand c;
            c.usedIdx = -1; c.early = early; c.fillr = 0; c.aff = 0;
            c.cost = (double)early * g.IC * (double)q * inst.coeffInv + ap.alpha * inst.coeffTra * (double)t.cost;
            cands.push_back(c);
        }
        sort(cands.begin(), cands.end(), [&](const Cand &a, const Cand &b) {
            if (a.cost != b.cost) return a.cost < b.cost;
            if (a.early != b.early) return a.early < b.early;
            if (a.fillr != b.fillr) return a.fillr > b.fillr;
            return a.usedIdx > b.usedIdx;
        });
        int newDef = -1;
        for (auto &c : cands) {
            if (q <= 0) break;
            if (c.usedIdx < 0) { newDef = -2; break; } // marker: new truck of this def preferred next
            UsedTruck &ut = used[c.usedIdx];
            const TruckDef &t = inst.trucks[ut.defIdx];
            double cap = ut.floorArea * utilRefill;
            double unitArea = (double)g.L * g.W / estStackCap(g, t);
            long long canArea = (long long)floor(max(0.0, (cap - ut.areaSum)) / max(unitArea, 1e-9));
            long long canW = (g.weight > 0) ? (long long)floor(max(0.0, (double)t.maxWeight - ut.weightSum) / g.weight) : (long long)1e18;
            long long take = min(q, min(canArea, canW));
            if (take <= 0) continue;
            ut.assign.push_back({gi, take});
            ut.areaSum += take * unitArea;
            ut.weightSum += take * g.weight;
            ut.keyUnits[g.p4key] += take;
            ut.dirty = true;
            q -= take;
        }
        (void)newDef;
        (void)uni;
        // leftovers -> open extras of best def (cheapest candidate)
        while (q > 0) {
            int ti = cs.empty() ? 0 : cs[0];
            double bestC = 1e18;
            for (int x : cs) {
                double c2 = (double)inst.trucks[x].cost * (1.0 + inst.coeffExtra);
                if (c2 < bestC) { bestC = c2; ti = x; }
            }
            const TruckDef &t = inst.trucks[ti];
            // prefer refilling an existing extra of this def
            auto it = byDef.find(ti);
            if (it != byDef.end()) {
                for (int j = (int)it->second.size() - 1; j >= 0 && q > 0; j--) {
                    UsedTruck &ut = used[it->second[j]];
                    if (!ut.extra) continue;
                    double cap = ut.floorArea * utilRefill;
                    double unitArea = (double)g.L * g.W / estStackCap(g, t);
                    long long canArea = (long long)floor(max(0.0, (cap - ut.areaSum)) / max(unitArea, 1e-9));
                    long long canW = (g.weight > 0) ? (long long)floor(max(0.0, (double)t.maxWeight - ut.weightSum) / g.weight) : (long long)1e18;
                    long long take = min(q, min(canArea, canW));
                    if (take > 0) {
                        ut.assign.push_back({gi, take});
                        ut.areaSum += take * unitArea;
                        ut.weightSum += take * g.weight;
                        ut.keyUnits[g.p4key] += take;
            ut.dirty = true;
                        q -= take;
                    }
                }
            }
            if (q <= 0) break;
            UsedTruck ut; ut.defIdx = ti; ut.extra = true;
            int seq = ++extraCount[ti];
            ut.outId = "Q" + t.id.substr(1) + "_" + to_string(seq);
            ut.extraSeq = seq;
            ut.floorArea = (double)t.L * t.W;
            double unitArea = (double)g.L * g.W / estStackCap(g, t);
            long long take = min(q, (long long)floor((ut.floorArea * utilRefill) / max(unitArea, 1e-9)));
            if (g.weight > 0) take = min(take, (long long)floor((double)t.maxWeight / g.weight));
            take = max(take, 1LL);
            ut.assign.push_back({gi, take});
            ut.areaSum += take * unitArea;
            ut.weightSum += take * g.weight;
            ut.keyUnits[g.p4key] += take;
            ut.dirty = true;
            q -= take;
            byDef[ti].push_back((int)used.size());
            used.push_back(move(ut));
        }
    }
}

static string validateSolution(Instance &inst, Solution &sol) {
    unordered_map<string, long long> need;
    for (auto &g : inst.groups) need[g.id] += g.qty;
    unordered_map<string, long long> got;
    for (auto &s : sol.stacks)
        for (auto &u : s.units) got[inst.groups[u.first].id] += u.second;
    for (auto &kv : need)
        if (got[kv.first] != kv.second)
            return "I1 mismatch item " + kv.first + " need=" + to_string(kv.second) + " got=" + to_string(got[kv.first]);
    unordered_set<string> truckIds;
    for (auto &ot : sol.trucks) {
        if (!truckIds.insert(ot.outId).second) return "duplicate truck id " + ot.outId;
        if (ot.stacks.empty()) return "truck without stacks " + ot.outId;
    }
    for (size_t si = 0; si < sol.stacks.size(); si++) {
        const OutStack &s = sol.stacks[si];
        if (s.truckIdx < 0 || s.truckIdx >= (int)sol.trucks.size()) return "bad truckIdx";
        const OutTruck &ot = sol.trucks[s.truckIdx];
        const TruckDef &t = inst.trucks[ot.defIdx];
        int hh = 0;
        for (size_t i = 0; i < s.units.size(); i++) {
            const ItemGroup &g = inst.groups[s.units[i].first];
            hh += g.H * (int)s.units[i].second;
            if (i > 0) hh -= g.nest * (int)s.units[i].second;
            else if (s.units[i].second > 1) hh -= g.nest * (int)(s.units[i].second - 1);
        }
        if (hh != s.zE) return "stack height mismatch";
        if (s.zE > t.H) return "P1 height";
        if (s.xO < 0 || s.xE > t.L) return "P1 x";
        if (s.yO < 0 || s.yE > t.W) return "P1 y";
        if (s.xE <= s.xO || s.yE <= s.yO) return "degenerate";
        const ItemGroup *b0 = &inst.groups[s.units[0].first];
        double above = 0;
        long long cnt = 0;
        int minISM = b0->ISM;
        for (size_t i = 0; i < s.units.size(); i++) {
            const ItemGroup &g = inst.groups[s.units[i].first];
            if (i == 0) above += g.weight * (s.units[i].second - 1);
            else above += g.weight * s.units[i].second;
            minISM = min(minISM, g.ISM);
            cnt += s.units[i].second;
            if (g.supplier != b0->supplier) return "S1 supplier";
            if (g.plant != b0->plant) return "S1 plant";
            if (g.code != b0->code) return "S1 code";
            if (g.sdock != b0->sdock) return "S1 sdock";
            if (g.pdock != b0->pdock) return "single-dock assumption";
            if (g.forcedOrient) {
                int want = (g.forcedOrient == 1) ? g.L : g.W;
                if (s.xE - s.xO != want) return "S4 orientation";
            }
        }
        if (cnt > minISM) return "S6 count";
        auto ma = t.maxAbove.find(b0->product);
        if (ma != t.maxAbove.end() && above > ma->second) return "S5 above";
        double dens = s.weight / ((double)(s.xE - s.xO) * (s.yE - s.yO) / 1e6);
        if (floor(dens) > t.maxDensity) return "S7 density";
        for (auto &u : s.units) {
            const ItemGroup &g = inst.groups[u.first];
            if (g.plant != t.plant) return "I2";
            if (!t.products.count(g.product)) return "I3";
            if (!t.supOrder.count(g.supplier)) return "I4";
            if (!t.sdockOrder.count(g.sdock)) return "P4 sdock missing";
            if (!t.pdockOrder.count(g.pdock)) return "P4 pdock missing";
            if (t.arrival < g.earliest || t.arrival > g.latest) return "I5";
        }
    }
    for (auto &ot : sol.trucks) {
        const TruckDef &t = inst.trucks[ot.defIdx];
        vector<const OutStack *> st;
        for (int si : ot.stacks) st.push_back(&sol.stacks[si]);
        double tw = 0;
        for (auto s : st) tw += s->weight;
        if (tw > t.maxWeight + 0.1) return "W1";
        for (size_t i = 0; i < st.size(); i++)
            for (size_t j = 0; j < st.size(); j++) {
                if (i == j) continue;
                const OutStack *a = st[i], *b = st[j];
                if (a->xO <= b->xO && b->xO < a->xE && !(b->yO >= a->yE || b->yE <= a->yO))
                    return "P2 overlap";
            }
        for (size_t i = 0; i < st.size(); i++) {
            const OutStack *s1 = st[i];
            if (s1->xO == 0) continue;
            if (st.size() == 1) return "P3 single not front";
            bool adj = false;
            for (size_t j = 0; j < st.size(); j++) {
                if (i == j) continue;
                const OutStack *s2 = st[j];
                if (s1->xO == s2->xE &&
                    ((s2->yO >= s1->yO && s2->yO <= s1->yE) ||
                     (s2->yE >= s1->yO && s2->yE <= s1->yE) ||
                     (s2->yE >= s1->yE && s2->yO <= s1->yO))) { adj = true; break; }
            }
            if (!adj) return "P3 no support";
        }
        for (size_t i = 0; i < st.size(); i++)
            for (size_t j = 0; j < st.size(); j++) {
                if (i == j) continue;
                const OutStack *s1 = st[i], *s2 = st[j];
                if (!(s1->xO < s2->xO)) continue;
                int so1, so2;
                auto i1 = t.supOrder.find(s1->supplier); if (i1 == t.supOrder.end()) return "P4 sup missing";
                auto i2 = t.supOrder.find(s2->supplier); if (i2 == t.supOrder.end()) return "P4 sup missing";
                so1 = i1->second; so2 = i2->second;
                if (so1 > so2) return "P4 supplier order";
                if (so1 == so2) {
                    auto d1 = t.sdockOrder.find(s1->sdock); if (d1 == t.sdockOrder.end()) return "P4 sdock missing";
                    auto d2 = t.sdockOrder.find(s2->sdock); if (d2 == t.sdockOrder.end()) return "P4 sdock missing";
                    if (d1->second > d2->second) return "P4 sdock order";
                    if (d1->second == d2->second) {
                        auto p1 = t.pdockOrder.find(s1->pdock); if (p1 == t.pdockOrder.end()) return "P4 pdock missing";
                        auto p2 = t.pdockOrder.find(s2->pdock); if (p2 == t.pdockOrder.end()) return "P4 pdock missing";
                        if (s1->pdock != s2->pdock && p1->second > p2->second) return "P4 pdock order";
                    }
                }
            }
        set<int> orders;
        for (auto s : st) orders.insert(t.supOrder.at(s->supplier));
        for (int mo : orders) {
            double tm = 0, mom = 0;
            for (auto s : st) {
                if (t.supOrder.at(s->supplier) > mo) continue;
                tm += s->weight; mom += (s->xO + (s->xE - s->xO) / 2.0) * s->weight;
            }
            if (tm <= 0) continue;
            double eje = mom / tm;
            double ejr = t.EJeh + t.EJhr - eje;
            double emh = (tm * ejr + t.EM * t.EJcr) / t.EJhr;
            double emr = tm + t.EM - emh;
            double emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
            if (emm > t.EMmm + 0.1) return "W2 middle";
            if (emr > t.EMmr + 0.1) return "W2 rear";
        }
    }
    return "";
}

static void writeSolution(Instance &inst, Solution &sol, const string &outDir, FILE *log) {
    string itemsPath = outDir + "/output_items.csv";
    string stacksPath = outDir + "/output_stacks.csv";
    string trucksPath = outDir + "/output_trucks.csv";
    FILE *fi = fopen(itemsPath.c_str(), "wb");
    FILE *fs = fopen(stacksPath.c_str(), "wb");
    FILE *ft = fopen(trucksPath.c_str(), "wb");
    if (!fi || !fs || !ft) { fprintf(stderr, "cannot write outputs\n"); exit(4); }
    fprintf(fi, "Item ident;Id truck;Id stack;Item code;X origin;Y origin;Z origin;X extremity;Y extremity;Z extremity\n");
    fprintf(fs, "Id truck;Id stack;Stack code;X origin;Y origin;Z origin;X extremity;Y extremity;Z extremity\n");
    fprintf(ft, "Id truck;Loaded length;Weight of loaded items;Volume of loaded items;EMm;EMr\n");
    int stackSeq = 0;
    for (auto &ot : sol.trucks) {
        const TruckDef &t = inst.trucks[ot.defIdx];
        int loadedLen = 0;
        double loadedW = 0, loadedV = 0;
        double tm = 0, mom = 0;
        for (int si : ot.stacks) {
            OutStack &s = sol.stacks[si];
            loadedLen = max(loadedLen, s.xE);
            loadedW += s.weight;
            tm += s.weight;
            mom += (s.xO + (s.xE - s.xO) / 2.0) * s.weight;
            for (size_t i = 0; i < s.units.size(); i++) {
                const ItemGroup &g = inst.groups[s.units[i].first];
                double v = (i == 0) ? (double)g.L * g.W * g.H / 1e9 : (double)g.L * g.W * (g.H - g.nest) / 1e9;
                loadedV += v * s.units[i].second;
            }
            string stackId = ot.outId + "_" + to_string(++stackSeq);
            fprintf(fs, "%s;%s;S;%d;%d;%d;%d;%d;%d\n",
                    ot.outId.c_str(), stackId.c_str(), s.xO, s.yO, 0, s.xE, s.yE, s.zE);
            int z = 0;
            bool first = true;
            for (size_t i = 0; i < s.units.size(); i++) {
                const ItemGroup &g = inst.groups[s.units[i].first];
                for (long long c = 0; c < s.units[i].second; c++) {
                    int z0, z1;
                    if (first) { z0 = 0; z1 = g.H; first = false; }
                    else { z0 = z; z1 = z0 + g.H - g.nest; }
                    fprintf(fi, "%s;%s;%s;A;%d;%d;%d;%d;%d;%d\n",
                            g.id.c_str(), ot.outId.c_str(), stackId.c_str(),
                            s.xO, s.yO, z0, s.xE, s.yE, z1);
                    z = z1;
                }
            }
        }
        double emm = 0, emr = 0;
        if (tm > 0) {
            double eje = mom / tm;
            double ejr = t.EJeh + t.EJhr - eje;
            double emh = (tm * ejr + t.EM * t.EJcr) / t.EJhr;
            emr = tm + t.EM - emh;
            emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
        }
        fprintf(ft, "%s;%d;%.3f;%.3f;%.3f;%.3f\n", ot.outId.c_str(), loadedLen, loadedW, loadedV, emm, emr);
    }
    fclose(fi); fclose(fs); fclose(ft);
    fprintf(log, "[solver] written outputs (trucks=%zu stacks=%zu)\n", sol.trucks.size(), sol.stacks.size());
}

static Solution solveNaiveSafe(Instance &inst, FILE *log) {
    Solution sol;
    unordered_map<int,int> extraSeq;
    vector<OutStack> allStacks;
    vector<OutTruck> outTrucks;
    for (size_t gi0 = 0; gi0 < inst.groups.size(); gi0++) {
        ItemGroup &g = inst.groups[gi0];
        vector<int> &cs = inst.groupCands[gi0];
        int base = cs[0];
        for (int c : cs) if (inst.trucks[c].arrDay > inst.trucks[base].arrDay) base = c;
        const TruckDef &t = inst.trucks[base];
        long long made = 0;
        while (made < g.qty) {
            int seq = ++extraSeq[base];
            OutTruck ot;
            ot.defIdx = base; ot.extra = true;
            ot.outId = "Q" + t.id.substr(1) + "_" + to_string(seq);
            int truckIdx = (int)outTrucks.size();
            int x = 0;
            int dx = (g.forcedOrient == 2) ? g.W : g.L;
            int wy = (g.forcedOrient == 2) ? g.L : g.W;
            long long perTruck = min(g.qty - made, (long long)(t.L / dx));
            perTruck = min(perTruck, (long long)floor((double)t.maxWeight / g.weight));
            if (perTruck < 1) perTruck = 1;
            vector<OutStack> placed;
            for (int i = 0; i < perTruck && made + (long long)placed.size() < g.qty; i++) {
                if (x + dx > t.L) break;
                OutStack os;
                os.truckIdx = truckIdx;
                os.xO = x; os.yO = 0; os.xE = x + dx; os.yE = wy; os.zE = g.H;
                os.weight = g.weight;
                os.supplier = g.supplier; os.sdock = g.sdock; os.pdock = g.pdock;
                os.code = g.code; os.product = g.product; os.orient = g.forcedOrient ? g.forcedOrient : 1;
                os.p4key = g.p4key;
                os.units.push_back({(int)gi0, 1});
                placed.push_back(move(os));
                x += dx;
            }
            int guard = 0;
            while (!axlePrefixOk(t, placed) && placed.size() > 1 && guard++ < 10000) {
                double tm = 0, mom = 0;
                for (auto &s : placed) { tm += s.weight; mom += (s.xO + (s.xE - s.xO) / 2.0) * s.weight; }
                double eje = tm > 0 ? mom / tm : 0;
                double ejr = t.EJeh + t.EJhr - eje;
                double emh = tm > 0 ? (tm * ejr + t.EM * t.EJcr) / t.EJhr : 0;
                double emm = (t.CM * t.CJfc + emh * t.CJfh) / t.CJfm;
                if (emm > t.EMmm + 0.1) placed.erase(placed.begin());
                else placed.pop_back();
                int x2 = 0;
                for (auto &s : placed) { int d2 = s.xE - s.xO; s.xO = x2; s.xE = x2 + d2; x2 += d2; }
            }
            if (!axlePrefixOk(t, placed) && placed.size() == 1) {
                sol.fatal = true;
                return sol;
            }
            int sbase = (int)allStacks.size();
            for (auto &s : placed) { allStacks.push_back(move(s)); ot.stacks.push_back(sbase++); made++; }
            outTrucks.push_back(move(ot));
        }
    }
    sol.trucks = move(outTrucks);
    sol.stacks = move(allStacks);
    recomputeCosts(inst, sol);
    fprintf(log, "[solver] naive-safe: extra=%d obj=%.0f\n", sol.nTrucksExtra, sol.objective);
    return sol;
}

int main(int argc, char **argv) {
    T0 = Clock::now();
    if (argc < 5) {
        fprintf(stderr, "usage: solver INPUT_DIR OUTPUT_DIR INTERNAL_SECONDS SEED\n");
        return 1;
    }
    string inDir = argv[1], outDir = argv[2];
    double internalSec = atof(argv[3]);
    unsigned seed = (unsigned)strtoul(argv[4], nullptr, 10);
    double cap = 780.0;
    FILE *log = stderr;
    fprintf(log, "[solver] v014 start in=%s out=%s internal=%.0f seed=%u\n", inDir.c_str(), outDir.c_str(), internalSec, seed);
    Instance inst;
    parseInto(inDir, inst);
    {
        long long totalItems = 0;
        for (auto &g : inst.groups) totalItems += g.qty;
        if (totalItems > 120000) cap = 840.0;
    }
    if (const char *e = getenv("Z_SEARCH_CAP")) cap = atof(e);
    g_deadline = min(internalSec - 15.0, cap);
    fprintf(log, "[solver] parsed groups=%zu trucks=%zu ids=%d cap=%.0f (%.1fs)\n",
            inst.groups.size(), inst.trucks.size(), inst.ids.size(), cap, elapsed());

    int nThreads = 8;
    if (const char *e = getenv("Z_THREADS")) nThreads = max(1, atoi(e));
    // tiny instances do not benefit from many threads; keep them anyway (cheap)
    struct Cfg { double util; bool affinity; int orderMode; double affNoise; double alpha; double beta; };
    vector<Cfg> cfgs;
    for (double al : {0.15, 0.4, 0.8, 1.5})
        for (double be : {0.05, 0.20})
            for (int om : {0, 2})
                cfgs.push_back({0.94, true, om, 0.0, al, be});
    cfgs.push_back({0.90, false, 0, 0.0, 0.3, 0.0});
    cfgs.push_back({0.98, true, 0, 0.0, 0.8, 0.10});

    mutex bestMx;
    Solution best;
    bool have = false;
    double lastWrite = 0.0;
    bool wroteOnce = false;

    // cross-entropy style elite parameter memory
    struct ParamSample { double util, alpha, beta; int om; };
    mutex eliteMx;
    vector<pair<double, ParamSample>> elite;
    auto addElite = [&](double obj, const ParamSample &p) {
        lock_guard<mutex> lk(eliteMx);
        elite.push_back({obj, p});
        if (elite.size() > 32) {
            sort(elite.begin(), elite.end(),
                 [](auto &a, auto &b) { return a.first < b.first; });
            elite.resize(12);
        }
    };
    auto sampleParams = [&](mt19937 &rng) {
        ParamSample p;
        lock_guard<mutex> lk(eliteMx);
        uniform_real_distribution<double> uni(0.0, 1.0);
        bool explore = elite.empty() || uni(rng) < 0.25;
        if (explore) {
            uniform_real_distribution<double> ud(0.85, 1.0), ad(0.08, 2.0), bd(0.0, 0.3);
            uniform_int_distribution<int> om(0, 2);
            p = {ud(rng), ad(rng), bd(rng), om(rng)};
        } else {
            uniform_int_distribution<int> ei(0, (int)elite.size() - 1);
            auto &e = elite[ei(rng)].second;
            normal_distribution<double> nd(0.0, 1.0);
            p.util = e.util + nd(rng) * 0.03;
            p.alpha = e.alpha * exp(nd(rng) * 0.35);
            p.beta = e.beta + nd(rng) * 0.05;
            uniform_int_distribution<int> om(0, 2);
            p.om = (uni(rng) < 0.5) ? e.om : om(rng);
        }
        p.util = min(1.0, max(0.80, p.util));
        p.alpha = min(3.0, max(0.05, p.alpha));
        p.beta = min(0.4, max(0.0, p.beta));
        p.om = min(2, max(0, p.om));
        return p;
    };

    auto worker = [&](int tid) {
        mt19937 rng(seed * 1000u + 7919u * (unsigned)tid + 13u);
        FILE *tlog = stderr;
        int iter = 0;
        Solution threadBest; // per-thread SA incumbent
        bool thHave = false;
        double temp0 = 0.004; // SA temperature as fraction of objective
        while (timeLeft(25.0)) {
            Cfg c;
            if (iter < (int)cfgs.size()) c = cfgs[(iter + tid) % cfgs.size()];
            else {
                ParamSample p = sampleParams(rng);
                c = {p.util, true, p.om, (iter % 2) ? 30.0 : 0.0, p.alpha, p.beta};
            }
            AssignParams ap;
            ap.util = c.util; ap.affinity = c.affinity; ap.orderMode = c.orderMode;
            ap.affNoise = c.affNoise; ap.alpha = c.alpha; ap.beta = c.beta;
            Solution s;
            bool doLns = thHave && (iter % 2) == 1;
            if (doLns) {
                Solution cur = threadBest; // copy SA incumbent
                vector<vector<OutStack>> layouts;
                vector<UsedTruck> used = solutionToAssignment(inst, cur, &layouts);
                uniform_real_distribution<double> uni(0.0, 1.0);
                double thresh = 0.35 + uni(rng) * 0.45;
                vector<UsedTruck> kept;
                vector<vector<OutStack>> keptCache;
                vector<pair<int,long long>> pool;
                for (size_t i = 0; i < used.size(); i++) {
                    double fill = used[i].areaSum / (used[i].floorArea * 0.97);
                    bool drop = (fill < thresh) || (uni(rng) < 0.06);
                    if (drop)
                        for (auto &kv : used[i].assign) pool.push_back(kv);
                    else {
                        kept.push_back(move(used[i]));
                        keptCache.push_back(move(layouts[i]));
                    }
                }
                if (pool.empty() || kept.empty()) { iter++; continue; }
                assignRefill(inst, kept, pool, ap, rng, 0.97);
                s = buildSolution(inst, kept, tlog, true, &keptCache);
            } else {
                vector<UsedTruck> used = assignGreedy(inst, ap, rng);
                s = buildSolution(inst, used, tlog, true);
            }
            if (!s.fatal) {
                consolidate(inst, s, tlog, 2);
                // SA acceptance into the thread incumbent
                bool accept = !thHave;
                if (!accept) {
                    double diff = (s.objective - threadBest.objective) / max(threadBest.objective, 1.0);
                    if (diff <= 0) accept = true;
                    else {
                        double temp = temp0 * max(0.05, 1.0 - elapsed() / max(g_deadline, 1.0));
                        uniform_real_distribution<double> uni(0.0, 1.0);
                        accept = uni(rng) < exp(-diff / max(temp, 1e-9));
                    }
                }
                if (accept) {
                    string err = validateSolution(inst, s);
                    if (err.empty()) {
                        threadBest = s; // keep as incumbent (copy)
                        thHave = true;
                        addElite(s.objective, ParamSample{c.util, c.alpha, c.beta, c.orderMode});
                        bool promising = false;
                        {
                            lock_guard<mutex> lk(bestMx);
                            promising = !have || s.objective < best.objective - 1e-9;
                        }
                        if (promising) {
                            lock_guard<mutex> lk(bestMx);
                            if (!have || s.objective < best.objective - 1e-9) {
                                fprintf(tlog, "[t%d] iter %d best obj=%.0f (%.1fs)\n", tid, iter, s.objective, elapsed());
                                best = move(s);
                                have = true;
                                if (!wroteOnce || (elapsed() - lastWrite > 60.0 && timeLeft(30.0))) {
                                    writeSolution(inst, best, outDir, tlog);
                                    wroteOnce = true;
                                    lastWrite = elapsed();
                                }
                            }
                        }
                    } else {
                        fprintf(tlog, "[t%d] iter %d INVALID: %s\n", tid, iter, err.c_str());
                    }
                }
            }
            iter++;
            if (iter > 200000) break;
        }
    };
    vector<thread> threads;
    for (int t = 1; t < nThreads; t++) threads.emplace_back(worker, t);
    worker(0);
    for (auto &th : threads) th.join();

    if (!have) {
        fprintf(log, "[solver] EMERGENCY naive-safe fallback\n");
        Solution n = solveNaiveSafe(inst, log);
        string err2 = n.fatal ? string("fatal") : validateSolution(inst, n);
        if (err2.empty()) { best = move(n); have = true; }
        else { fprintf(log, "[solver] naive-safe invalid: %s\n", err2.c_str()); return 5; }
    }
    if (!have) return 5;
    writeSolution(inst, best, outDir, log);
    fprintf(log, "[solver] DONE obj=%.1f transport=%.1f inv=%.1f planned=%d extra=%d wall=%.1fs threads=%d\n",
            best.objective, best.transport, best.inventory, best.nTrucksPlanned, best.nTrucksExtra, elapsed(), nThreads);
    return 0;
}
