#ifndef REPAIR_H
#define REPAIR_H

#include <random>
#include "../solution/solution.hpp"

/*
 * Violation-driven feasibility repair (min-conflicts with bounded lookahead).
 *
 * The reference ALNS scores candidates with a big-M extended objective
 * (1e5 per unit of overload / per broken exclusion). That makes violations
 * dominate the *reported* objective, but it does not give the search a way out
 * of states where every single-invervention move increases the penalty, so the
 * reference implementation ends infeasible on many instances.
 *
 * repair enforces a strict lexicographic order instead:
 *      1. minimise  overload + underload + broken exclusions
 *      2. then minimise  the official risk objective
 * It repeatedly picks the worst overloaded (resource, time) cell (or a broken
 * exclusion pair), samples the interventions responsible for it, evaluates every
 * alternative start time with the incremental estimators, and applies the best
 * move. When no improving move exists it performs a bounded random walk so the
 * procedure cannot stall in a local minimum.
 */

struct RepairStats {
    long long steps = 0;
    long long applied = 0;
    long long random_walks = 0;
    bool feasible = false;
    fitness_t violation = 0.0;
    fitness_t best_seen = 0.0;
};

RepairStats feasibility_repair(Solution &solution, int max_steps, std::default_random_engine *rng);

/* total scaled violation of a candidate estimate (overload + underload + exclusions) */
inline fitness_t violation_of(const Objective &o) {
    return o.workload_overuse + o.workload_underuse + (fitness_t)o.exclusion_penalty;
}

/* lexicographic comparison: fewer violations wins, otherwise better risk objective */
inline bool repair_better(fitness_t v_new, fitness_t obj_new, fitness_t v_cur, fitness_t obj_cur) {
    if (v_new < v_cur - 1e-9) return true;
    if (v_new > v_cur + 1e-9) return false;
    return obj_new < obj_cur - 1e-9;
}

#endif
