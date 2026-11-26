#include "repair.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../params.hpp"

using namespace std;

namespace {

struct Cell {
    fitness_t amount;
    uint_t idx;
};

/* (resource,time) cells whose load exceeds the upper bound, worst first */
void collect_overloads(Solution &s, vector<Cell> &out) {
    out.clear();
    Instance *inst = s.instance;
    const uint_t T = inst->get_horizon_num();
    for (uint_t r : inst->get_resources()) {
        for (uint_t t = 0; t < T; ++t) {
            const fitness_t load = s.resource_use[r][t];
            const fitness_t cap = inst->u[inst->get_ul_index(r, t + 1)];
            if (load > cap + NUMERIC_TOLERANCE) out.push_back({load - cap, r * T + t});
        }
    }
    sort(out.begin(), out.end(), [](const Cell &a, const Cell &b) { return a.amount > b.amount; });
}

/* (resource,time) cells whose load is below the lower bound, worst first */
void collect_underloads(Solution &s, vector<Cell> &out) {
    out.clear();
    Instance *inst = s.instance;
    const uint_t T = inst->get_horizon_num();
    for (uint_t r : inst->get_resources()) {
        for (uint_t t = 0; t < T; ++t) {
            const fitness_t load = s.resource_use[r][t];
            const fitness_t floor_ = inst->l[inst->get_ul_index(r, t + 1)];
            if (load < floor_ - NUMERIC_TOLERANCE) out.push_back({floor_ - load, r * T + t});
        }
    }
    sort(out.begin(), out.end(), [](const Cell &a, const Cell &b) { return a.amount > b.amount; });
}

inline bool active_at(Instance *inst, uint_t i, uint_t st, uint_t t) {
    const uint_t d = inst->delta[inst->get_delta_index(i, st)];
    return t >= st && t < st + d;
}

/* interventions currently loading (r,t) */
void collect_contributors(Solution &s, uint_t r, uint_t t, vector<uint_t> &out) {
    out.clear();
    Instance *inst = s.instance;
    for (uint_t i : s.scheduled) {
        const uint_t st = s.get_start_time(i);
        if (!active_at(inst, i, st, t)) continue;
        if (inst->r[inst->get_r_index(i, st, r, t)] > 0.0) out.push_back(i);
    }
}

/* sample a broken exclusion pair; false when none found within the sample budget */
bool sample_broken_exclusion(Solution &s, std::default_random_engine *rng, uint_t &o1, uint_t &o2) {
    Instance *inst = s.instance;
    const auto &pairs = inst->get_exclusion_pairs();
    if (pairs.empty()) return false;
    uniform_int_distribution<size_t> pick(0, pairs.size() - 1);
    for (int attempt = 0; attempt < 128; ++attempt) {
        const auto &p = pairs[pick(*rng)];
        if (!s.is_scheduled(p.first) || !s.is_scheduled(p.second)) continue;
        const uint_t st1 = s.get_start_time(p.first);
        const uint_t st2 = s.get_start_time(p.second);
        const uint_t d1 = inst->delta[inst->get_delta_index(p.first, st1)];
        const uint_t d2 = inst->delta[inst->get_delta_index(p.second, st2)];
        for (uint_t t : inst->get_exclusion_season(p.first, p.second)) {
            if (t >= st1 && t >= st2 && t < st1 + d1 && t < st2 + d2) {
                o1 = p.first;
                o2 = p.second;
                return true;
            }
        }
    }
    return false;
}

struct MoveEval {
    bool found = false;
    uint_t t = 0;
    fitness_t violation = 0.0;
    fitness_t objective = 1e300;
};

struct PairEval {
    bool found = false;
    uint_t t1 = 0;
    uint_t t2 = 0;
    fitness_t violation = 0.0;
    fitness_t objective = 1e300;
};

/*
 * Joint move for a broken exclusion pair: some conflicts cannot be removed by
 * moving either intervention alone (each single move creates another conflict),
 * so sample start pairs for both at once.
 */
void evaluate_pair(Solution &s, uint_t i1, uint_t i2, std::default_random_engine *rng,
                   PairEval &best, fitness_t cur_v, fitness_t cur_obj) {
    Instance *inst = s.instance;
    const uint_t steer = inst->get_t_max(i1);
    const uint_t smax = inst->get_t_max(i2);
    if (steer == 0 || smax == 0) return;
    vector<uint_t> c1, c2;
    const uint_t SAMPLE = 14;
    if (steer <= SAMPLE) {
        for (uint_t t = 1; t <= steer; ++t) c1.push_back(t);
    } else {
        for (uint_t k = 0; k < SAMPLE; ++k) c1.push_back(1u + (uint_t)(rng->operator()() % steer));
    }
    if (smax <= SAMPLE) {
        for (uint_t t = 1; t <= smax; ++t) c2.push_back(t);
    } else {
        for (uint_t k = 0; k < SAMPLE; ++k) c2.push_back(1u + (uint_t)(rng->operator()() % smax));
    }
    Solution base = s;
    base.unschedule(i1);
    base.unschedule(i2);
    for (uint_t a : c1) {
        base.schedule(i1, a);
        for (uint_t b : c2) {
            if (b == s.get_start_time(i2) && a == s.get_start_time(i1)) continue;
            const Objective o = base.estimate_schedule(i2, b);
            const fitness_t v = violation_of(o);
            const fitness_t obj = o.final_objective;
            if (repair_better(v, obj, cur_v, cur_obj) &&
                (!best.found || repair_better(v, obj, best.violation, best.objective))) {
                best.found = true;
                best.t1 = a;
                best.t2 = b;
                best.violation = v;
                best.objective = obj;
            }
        }
        base.unschedule(i1);
    }
}

/* evaluate every allowed start of `i` against the current solution (lexicographic) */
void evaluate_intervention(Solution &s, uint_t i, MoveEval &best, MoveEval &sideways,
                           fitness_t cur_v, fitness_t cur_obj) {
    Instance *inst = s.instance;
    Solution base = s;
    base.unschedule(i);
    const uint_t tmax = inst->get_t_max(i);
    fitness_t best_v = cur_v;
    fitness_t best_obj = cur_obj;
    fitness_t side_v = cur_v;
    fitness_t side_obj = cur_obj;
    for (uint_t t = 1; t <= tmax; ++t) {
        const Objective o = base.estimate_schedule(i, t);
        const fitness_t v = violation_of(o);
        const fitness_t obj = o.final_objective;
        if (repair_better(v, obj, best_v, best_obj)) {
            best_v = v;
            best_obj = obj;
            best.found = true;
            best.t = t;
            best.violation = v;
            best.objective = obj;
        } else if (fabs(v - side_v) <= 1e-9 && obj < side_obj - 1e-9) {
            side_v = v;
            side_obj = obj;
            sideways.found = true;
            sideways.t = t;
            sideways.violation = v;
            sideways.objective = obj;
        }
    }
}

}  // namespace

RepairStats feasibility_repair(Solution &solution, int max_steps, std::default_random_engine *rng) {
    RepairStats st;
    Instance *inst = solution.instance;
    const uint_t T = inst->get_horizon_num();
    vector<Cell> overloads, underloads;
    vector<uint_t> contributors;
    const uint_t MAX_LOOK = 10;      /* candidate interventions examined per step */
    const int STALL_LIMIT = 250;     /* steps without any violation decrease */
    int stall = 0;
    st.best_seen = solution.workload_overuse + solution.workload_underuse +
                   (fitness_t)solution.exclusion_penalty;

    for (int step = 0; step < max_steps; ++step) {
        if (stop()) break;
        fitness_t total_v = solution.workload_overuse + solution.workload_underuse +
                            (fitness_t)solution.exclusion_penalty;
        if (total_v <= 1e-9) break;
        st.steps++;

        /* give up quickly once the procedure stops making headway, so a hopeless
         * repair cannot swallow the whole search budget */
        if (total_v < st.best_seen - 1e-12) {
            st.best_seen = total_v;
            stall = 0;
        } else if (++stall > STALL_LIMIT) {
            break;
        }

        collect_overloads(solution, overloads);
        if (!overloads.empty() && solution.exclusion_penalty > 0) {
            /* interleave exclusion repair: roughly one step in three */
            if ((rng->operator()() % 3u) == 0) overloads.clear();
        }

        uint_t best_i = 0;
        MoveEval best, sideways;
        bool have_candidates = false;
        vector<uint_t> candidates;

        if (!overloads.empty()) {
            const size_t top = min<size_t>(overloads.size(), 8);
            uniform_int_distribution<size_t> pick(0, top - 1);
            const uint_t cell = overloads[pick(*rng)].idx;
            collect_contributors(solution, cell / T, cell % T, contributors);
            if (contributors.empty()) continue;
            shuffle(contributors.begin(), contributors.end(), *rng);
            const size_t take = min<size_t>(contributors.size(), MAX_LOOK);
            candidates.assign(contributors.begin(), contributors.begin() + take);
            have_candidates = true;
        } else if (solution.exclusion_penalty > 0) {
            uint_t a = 0, b = 0;
            if (!sample_broken_exclusion(solution, rng, a, b)) continue;
            candidates = {a, b};
            have_candidates = true;
        } else if (solution.workload_underuse > 1e-9) {
            collect_underloads(solution, underloads);
            if (underloads.empty()) break;
            const size_t top = min<size_t>(underloads.size(), 8);
            uniform_int_distribution<size_t> pick(0, top - 1);
            const uint_t cell = underloads[pick(*rng)].idx;
            const uint_t r = cell / T;
            const uint_t t = cell % T;
            contributors.clear();
            for (uint_t i : solution.scheduled) {
                const uint_t st = solution.get_start_time(i);
                if (active_at(inst, i, st, t)) continue;
                bool covers = false;
                const uint_t tmax = inst->get_t_max(i);
                for (uint_t tt = 1; tt <= tmax && !covers; ++tt) {
                    if (!active_at(inst, i, tt, t)) continue;
                    if (inst->r[inst->get_r_index(i, tt, r, t)] > 0.0) covers = true;
                }
                if (covers) contributors.push_back(i);
                if (contributors.size() > 300) break;
            }
            if (contributors.empty()) continue;
            shuffle(contributors.begin(), contributors.end(), *rng);
            const size_t take = min<size_t>(contributors.size(), MAX_LOOK);
            candidates.assign(contributors.begin(), contributors.begin() + take);
            have_candidates = true;
        } else {
            break;
        }
        if (!have_candidates) continue;

        const fitness_t cur_v = total_v;
        const fitness_t cur_obj = solution.final_objective;
        for (uint_t cand : candidates) {
            evaluate_intervention(solution, cand, best, sideways, cur_v, cur_obj);
            if (best.found) { best_i = cand; break; }
        }

        /* a broken exclusion often needs a joint move: try both partners together */
        PairEval pair;
        if (!best.found && solution.exclusion_penalty > 0 && candidates.size() == 2) {
            evaluate_pair(solution, candidates[0], candidates[1], rng, pair, cur_v, cur_obj);
            if (pair.found) {
                solution.unschedule(candidates[0]);
                solution.unschedule(candidates[1]);
                solution.schedule(candidates[0], pair.t1);
                solution.schedule(candidates[1], pair.t2);
                st.applied++;
                continue;
            }
        }

        if (best.found) {
            solution.unschedule(best_i);
            solution.schedule(best_i, best.t);
            st.applied++;
        } else if (sideways.found && (rng->operator()() % 4u) == 0) {
            const uint_t cand = candidates[rng->operator()() % candidates.size()];
            if (sideways.t != solution.get_start_time(cand)) {
                solution.unschedule(cand);
                solution.schedule(cand, sideways.t);
                st.applied++;
            }
        } else {
            /* bounded random walk to escape a min-conflicts plateau */
            const uint_t cand = candidates[rng->operator()() % candidates.size()];
            const uint_t tmax = inst->get_t_max(cand);
            if (tmax > 1) {
                uniform_int_distribution<uint_t> pick(1, tmax);
                const uint_t nt = pick(*rng);
                if (nt != solution.get_start_time(cand)) {
                    solution.unschedule(cand);
                    solution.schedule(cand, nt);
                    st.random_walks++;
                }
            }
        }
    }

    st.violation = solution.workload_overuse + solution.workload_underuse +
                   (fitness_t)solution.exclusion_penalty;
    st.feasible = st.violation <= 1e-9;
    return st;
}
