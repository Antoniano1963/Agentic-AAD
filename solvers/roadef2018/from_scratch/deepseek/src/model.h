// ============================================================================
//  ROADEF / EURO 2018 Cutting Optimization -- shared model and file formats.
//
//  Everything here was written for this task; the only external artefact used is
//  the official checker, and only to validate the produced solutions.
//
//  Verified geometry of the official format (see docs/problem.txt, rules.pdf and
//  the checker sources): CUT equals the depth of the node in the tree, so the
//  split orientation alternates strictly with depth
//
//      depth 1 : vertical strips   (children share y and height)
//      depth 2 : horizontal rows   (children share x and width)
//      depth 3 : vertical columns  (children share y and height)
//      depth 4 : horizontal split  (children share x and width)
//
//  A node at depth 1 must additionally have width in [min1Cut, max1Cut].  Waste
//  must not overlap an item, must not have a cut line through a defect, must be
//  at least minWaste in both dimensions, and every defect must be contained in
//  some leaf.
// ============================================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace ro2018 {

// ---------------------------------------------------------------- parameters
struct Params {
    int nPlates = 100;
    int W = 6000;
    int H = 3210;
    int min1Cut = 100;
    int max1Cut = 3500;
    int min2Cut = 100;
    int minWaste = 20;
};

struct Item {
    int id = 0;
    int len = 0;   // LENGTH_ITEM
    int wid = 0;   // WIDTH_ITEM
    int stack = 0;
    int seq = 0;
    long long area = 0;
};

struct Defect { int x = 0, y = 0, w = 0, h = 0; };

struct Instance {
    Params par;
    std::vector<Item> items;
    std::vector<std::vector<Defect>> defs;   // indexed by plate id
    std::vector<int> order;                  // global precedence order
    std::vector<int> orderPos;               // position of an item inside `order`
    std::vector<std::vector<int>> stackPos;  // positions of a stack's items in `order`
    std::vector<std::vector<int>> stackItems;  // item indices per stack, in order
    int stacks = 0;
    long long totalArea = 0;
    int maxItemDim = 0;
};

// ---------------------------------------------------------------- utilities
inline double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::vector<std::string> splitSemi(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ';') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    for (auto& c : out) c = trim(c);
    return out;
}

inline bool readWhole(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB &&
        (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

inline std::vector<std::vector<std::string>> readCsv(const std::string& path) {
    std::string body;
    if (!readWhole(path, body)) return {};
    std::vector<std::vector<std::string>> rows;
    std::stringstream ss(body);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (first) { first = false; continue; }   // header
        rows.push_back(splitSemi(line));
    }
    return rows;
}

inline int toInt(const std::string& s) {
    if (s.empty()) return 0;
    return (int)std::strtol(s.c_str(), nullptr, 10);
}

inline bool rectHitsDefects(const std::vector<Defect>& defs, int x, int y, int w, int h) {
    for (const Defect& d : defs)
        if (x < d.x + d.w && d.x < x + w && y < d.y + d.h && d.y < y + h) return true;
    return false;
}

// ---------------------------------------------------------------- parsing
inline void parseParams(const std::string& path, Params& p) {
    for (auto& r : readCsv(path)) {
        if (r.size() < 2) continue;
        const int v = toInt(r[1]);
        const std::string& k = r[0];
        if (k == "nPlates") p.nPlates = v;
        else if (k == "widthPlates") p.W = v;
        else if (k == "heightPlates") p.H = v;
        else if (k == "min1Cut") p.min1Cut = v;
        else if (k == "max1Cut") p.max1Cut = v;
        else if (k == "min2Cut") p.min2Cut = v;
        else if (k == "minWaste") p.minWaste = v;
    }
}

inline bool parseBatch(const std::string& path, Instance& inst) {
    auto rows = readCsv(path);
    if (rows.empty()) return false;
    for (auto& r : rows) {
        if (r.size() < 5) continue;
        Item it;
        it.id = toInt(r[0]);
        it.len = toInt(r[1]);
        it.wid = toInt(r[2]);
        it.stack = toInt(r[3]);
        it.seq = toInt(r[4]);
        if (it.len <= 0 || it.wid <= 0) continue;
        it.area = (long long)it.len * it.wid;
        inst.maxItemDim = std::max(inst.maxItemDim, std::max(it.len, it.wid));
        inst.totalArea += it.area;
        inst.items.push_back(it);
    }
    return !inst.items.empty();
}

inline void parseDefects(const std::string& path, Instance& inst) {
    inst.defs.assign(inst.par.nPlates, {});
    for (auto& r : readCsv(path)) {
        if (r.size() < 6) continue;
        const int pid = toInt(r[1]);
        if (pid < 0 || pid >= inst.par.nPlates) continue;
        Defect d;
        d.x = toInt(r[2]);
        d.y = toInt(r[3]);
        d.w = toInt(r[4]);
        d.h = toInt(r[5]);
        if (d.w <= 0 || d.h <= 0) continue;
        inst.defs[pid].push_back(d);
    }
}

// ---------------------------------------------------------------- item order
// Merge the stacks by position: at each step take the item whose position inside
// its own stack is the smallest.  Reading items in this order can never violate
// the stack sequence of the official checker, whatever the layout is.
inline void buildOrder(Instance& inst) {
    int nStacks = 0;
    for (const Item& it : inst.items) nStacks = std::max(nStacks, it.stack + 1);
    std::vector<std::vector<int>> per(nStacks);
    for (size_t i = 0; i < inst.items.size(); ++i) per[inst.items[i].stack].push_back((int)i);
    for (auto& v : per)
        std::sort(v.begin(), v.end(), [&](int a, int b) {
            if (inst.items[a].seq != inst.items[b].seq) return inst.items[a].seq < inst.items[b].seq;
            return inst.items[a].id < inst.items[b].id;
        });
    std::vector<size_t> head(nStacks, 0);
    inst.order.clear();
    inst.order.reserve(inst.items.size());
    while ((int)inst.order.size() < (int)inst.items.size()) {
        int best = -1, bestStack = -1;
        for (size_t s = 0; s < per.size(); ++s) {
            if (head[s] >= per[s].size()) continue;
            const int idx = per[s][head[s]];
            if (best < 0 || inst.items[idx].seq < inst.items[best].seq ||
                (inst.items[idx].seq == inst.items[best].seq &&
                 inst.items[idx].id < inst.items[best].id)) {
                best = idx;
                bestStack = (int)s;
            }
        }
        if (best < 0) break;
        inst.order.push_back(best);
        ++head[bestStack];
    }
    // index the order so that "does this stack still have an item?" is O(1)
    inst.stacks = nStacks;
    inst.orderPos.assign(inst.items.size(), -1);
    inst.stackPos.assign(nStacks, {});
    inst.stackItems.assign(nStacks, {});
    for (size_t k = 0; k < inst.order.size(); ++k) {
        const int ii = inst.order[k];
        inst.orderPos[ii] = (int)k;
        const int s = inst.items[ii].stack;
        inst.stackPos[s].push_back((int)k);
        inst.stackItems[s].push_back(ii);
    }
}

}  // namespace ro2018
