// ============================================================================
//  ROADEF / EURO 2018 Cutting Optimization -- solver entry point.
//
//  usage: solver --instance <prefix> --params <global_param.csv>
//                --output <solution.csv> [--time-limit <s>] [--seed <n>]
//                [--threads <n>]
//
//  <prefix>_batch.csv and <prefix>_defects.csv are the official input files.
//
//  The solver spends its whole wall-clock budget in a parallel multi-start
//  search: every worker repeatedly draws a work unit (an orientation policy, a
//  jitter level and a reserved width for the last plate), packs the instance,
//  validates the pattern with an independent checker and reports the better
//  solution.  A solution is better when it uses fewer plates, or the same number
//  of plates with a wider residual on the last plate, which is exactly the
//  objective defined in TASK.md:
//
//      waste = nPlates*W*H - W*residualWidth - itemArea
// ============================================================================
#include "model.h"
#include "packer.h"
#include "plan.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ro2018;

namespace {

struct Best {
    std::mutex mu;
    std::vector<PlatePlan> plates;
    int plateCount = 0;
    int residualW = -1;
    long long waste = 0;
    double wastePct = 0.0;
    bool valid = false;
    long long iterations = 0;
    std::string source;

    // fewer plates first, then a wider residual
    bool better(int m, int resid) const {
        if (!valid) return true;
        if (m != plateCount) return m < plateCount;
        return resid > residualW;
    }
};

struct WorkUnit {
    int orient = 0;
    double jitter = 0.0;
    int lookahead = 80;
    int widthCands = 8;
    int rowMode = 0;    // 0 = most area, 1 = most items, 2 = best area per width
};

void usage() {
    std::fprintf(stderr,
                 "usage: solver --instance <prefix> --params <global_param.csv> "
                 "--output <out.csv> [--time-limit <s>] [--seed <n>] [--threads <n>]\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string prefix, paramsPath, outPath;
    double timeLimit = 900.0;
    unsigned long long seed = 1;
    int threads = 8;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (a == "--instance") prefix = next();
        else if (a == "--params") paramsPath = next();
        else if (a == "--output") outPath = next();
        else if (a == "--time-limit") timeLimit = std::atof(next().c_str());
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--threads") threads = std::max(1, std::atoi(next().c_str()));
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "solver: unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (prefix.empty() || outPath.empty()) { usage(); return 2; }

    const double t0 = nowSec();
    // keep a margin for writing the solution and exiting
    const double workDeadline = t0 + std::max(1.0, timeLimit - std::max(2.0, timeLimit * 0.02));

    Instance inst;
    if (!paramsPath.empty()) parseParams(paramsPath, inst.par);
    if (!parseBatch(prefix + "_batch.csv", inst)) {
        std::fprintf(stderr, "solver: cannot read %s_batch.csv\n", prefix.c_str());
        return 3;
    }
    parseDefects(prefix + "_defects.csv", inst);
    buildOrder(inst);

    const long long plateArea = (long long)inst.par.W * inst.par.H;
    const int lb = (int)((inst.totalArea + plateArea - 1) / plateArea);
    std::fprintf(stderr,
                 "[solver] items=%zu area=%lld lowerBoundPlates=%d W=%d H=%d seed=%llu "
                 "threads=%d timeLimit=%.0f\n",
                 inst.items.size(), inst.totalArea, lb, inst.par.W, inst.par.H, seed, threads,
                 timeLimit);

    Best best;
    std::atomic<long long> counter{0};
    int minPlates = -1;              // fewest plates found so far
    std::atomic<int> bestResidual{-1};

    auto worker = [&](int tid) {
        std::mt19937_64 rng(seed * 1000003ULL + (unsigned long long)tid * 7919ULL + 12345ULL);
        Packer packer(inst);
        while (nowSec() < workDeadline) {
            const long long u = counter.fetch_add(1);
            WorkUnit wu;
            switch ((int)(u % 16)) {
                case 0: wu = {0, 0.0, 80, 8, 0}; break;      // deterministic baseline
                case 1: wu = {1, 0.0, 80, 8, 0}; break;      // as read
                case 2: wu = {2, 0.0, 80, 8, 0}; break;      // rotated
                case 3: wu = {0, 0.0, 200, 14, 0}; break;    // wider lookahead
                case 4: wu = {0, 0.0, 40, 5, 0}; break;      // cheap, many restarts
                case 5: wu = {1, 0.0, 200, 14, 0}; break;
                case 6: wu = {2, 0.0, 200, 14, 0}; break;
                case 7: wu = {0, 1.0, 80, 8, 0}; break;      // randomised tie-breaks
                case 8: wu = {0, 0.5, 40, 5, 0}; break;
                case 9: wu = {0, 2.0, 120, 10, 0}; break;    // strong diversification
                case 10: wu = {1, 1.5, 80, 8, 0}; break;
                case 11: wu = {0, 0.0, 120, 10, 1}; break;   // pack as many items as possible
                case 12: wu = {0, 1.5, 120, 10, 1}; break;
                case 13: wu = {0, 0.0, 120, 10, 2}; break;   // best area per width
                case 14: wu = {2, 1.0, 200, 14, 2}; break;
                default: wu = {0, 0.0, 300, 20, 0}; break;   // deepest lookahead
            }
            PackOptions opt;
            opt.orient = wu.orient;
            opt.jitter = wu.jitter;
            opt.lookahead = wu.lookahead;
            opt.widthCands = wu.widthCands;
            opt.minResidual = 40;
            opt.rowMode = wu.rowMode;
            int mp = minPlates;
            if (mp < 0) mp = inst.par.nPlates;
            if (u % 4 == 0) opt.maxPlates = mp;                  // match the best
            else opt.maxPlates = std::max(1, mp - 1);            // try one fewer
            PackResult res = packer.pack(opt, rng);
            if (!res.complete) continue;
            ValidateResult vr = validatePlan(inst, res.plates);
            if (!vr.ok) continue;
            const int m = (int)res.plates.size();
            {
                std::lock_guard<std::mutex> lock(best.mu);
                if (minPlates < 0 || m < minPlates) minPlates = m;
            }
            std::lock_guard<std::mutex> lock(best.mu);
            best.iterations++;
            if (best.better(m, res.residualW)) {
                best.plates = res.plates;
                best.plateCount = m;
                best.residualW = res.residualW;
                best.waste = res.waste;
                best.wastePct = res.wastePct;
                best.valid = true;
                bestResidual.store(res.residualW);
                char tag[96];
                std::snprintf(tag, sizeof tag, "orient=%d jitter=%.0f lookahead=%d", wu.orient,
                              wu.jitter, wu.lookahead);
                best.source = tag;
                std::fprintf(stderr,
                             "[solver] t=%5.1fs iter=%lld plates=%d residualW=%d waste=%lld "
                             "waste%%=%.4f (%s)\n",
                             nowSec() - t0, best.iterations, m, res.residualW, res.waste,
                             res.wastePct, tag);
            }
        }
        // final refinement: with the best plate count fixed, cap the content
        // width of every plate so that the residual of the final plate grows
        {
            const int mp = minPlates;
            if (mp > 0) {
                int bestCap = 0;
                for (int cap = 0; cap + inst.par.min1Cut <= inst.par.W; cap += 25) {
                    if (nowSec() >= workDeadline + 1.0) break;
                    PackOptions opt;
                    opt.maxPlates = mp;
                    opt.orient = 0;
                    opt.jitter = 0.0;
                    opt.lookahead = 240;
                    opt.widthCands = 20;
                    opt.widthCap = cap;
                    opt.minResidual = std::max(40, cap);
                    PackResult res = packer.pack(opt, rng);
                    if (!res.complete) break;          // cap too tight
                    ValidateResult vr = validatePlan(inst, res.plates);
                    if (!vr.ok) continue;
                    if ((int)res.plates.size() != mp) break;
                    bestCap = cap;
                    std::lock_guard<std::mutex> lock(best.mu);
                    best.iterations++;
                    if (best.better(mp, res.residualW)) {
                        best.plates = res.plates;
                        best.plateCount = mp;
                        best.residualW = res.residualW;
                        best.waste = res.waste;
                        best.wastePct = res.wastePct;
                        best.valid = true;
                        best.source = "width cap sweep";
                    }
                }
                std::fprintf(stderr, "[solver] width cap sweep reached %d (residualW=%d)\n", bestCap,
                             best.residualW);
            }
        }
    };

    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker, t);
    for (auto& th : pool) th.join();

    std::fprintf(stderr, "[solver] minPlates=%d bestResidual=%d\n", minPlates,
                 bestResidual.load());
    std::fprintf(stderr, "[solver] search done: %lld validated packings in %.1fs (best=%s)\n",
                 (long long)best.iterations, nowSec() - t0,
                 best.valid ? best.source.c_str() : "none");
    if (!best.valid) {
        std::fprintf(stderr, "solver: FAILED to build a feasible solution\n");
        return 4;
    }

    EmitStats es;
    if (!writeSolution(inst, best.plates, outPath, es)) {
        std::fprintf(stderr, "solver: cannot write %s\n", outPath.c_str());
        return 5;
    }
    std::fprintf(stderr,
                 "[solver] DONE plates=%d residualW=%d waste=%lld waste%%=%.4f nodes=%d "
                 "time=%.2fs\n",
                 es.plates, es.residualW, es.waste, es.wastePct, es.nodes, nowSec() - t0);
    return 0;
}
