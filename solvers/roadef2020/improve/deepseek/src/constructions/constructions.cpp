#include "constructions.hpp"

using namespace std;

Solution dfs_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tdfs" << endl << "}" << endl;
#endif
    uint_t task;
    stack <Solution> S;
    S.push(Solution(instance, engine));
    uint_t count = 0;
    while (!S.empty()) {
        Solution cs = S.top(); /* retrieve element on top */
        if (!cs.has_unscheduled()) return cs;
        S.pop(); /* remove element from top */
        task = cs.get_first_unscheduled(); /* get first unscheduled intervention */
        for (uint_t time = instance->get_t_max(task); time > 0; --time) {
            Solution cs_ = cs;
            cs_.schedule(task, time);
            if (cs_.is_valid()) {
                S.push(cs_);
            }
        }
    }
    return Solution(instance, engine);
}

Solution dfs_optimum_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tdfs_optimum" << endl << "}" << endl;
#endif
    /* create variable for best solution and set its fitness to 0 */
    Solution best(instance, engine);
    best.final_objective = numeric_limits<fitness_t>::max();
    uint_t task;
    stack <Solution> S;
    S.push(Solution(instance, engine));
    uint_t count = 0;
    while (!S.empty()) {
        Solution cs = S.top(); /* retrieve element on top */
        S.pop(); /* remove element from top */
        if (!cs.has_unscheduled()) {
            if (cs.final_objective < best.final_objective) {
                best = cs;
            }
            continue;
        }
        task = cs.get_first_unscheduled(); /* get first unscheduled intervention */
        for (uint_t time = instance->get_t_max(task); time > 0; --time) {
            Solution cs_ = cs;
            cs_.schedule(task, time);
            if (cs_.is_valid()) {
                S.push(cs_);
            }
        }
    }
    return best;
}


/*
 * Build a solution by repeatedly calling random_insert
 */
Solution random_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\trandom" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) random_insert(s);
    return s;
}

/*
 * Build a solution  by repeatedly calling cheapest_insert
 */
Solution cheapest_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tcheapest" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            cheapest_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling lrd1_insert
 */
Solution lrd1_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tlrd1" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            lrd1_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling lrd2_insert
 */
Solution lrd2_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tlrd2" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            lrd2_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling hrd_insert
 */
Solution hrd_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\thrd" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            hrd_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

Solution fixed_order_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tfixed_order" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) fixed_order_insert(s);
    return s;
}

/*
 * Build a solution  by repeatedly calling longest1_insert
 */
Solution longest1_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tlongest1" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            longest1_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling longest2_insert
 */
Solution longest2_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tlongest2" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            longest2_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling me_insert
 */
Solution most_exclusions_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tme" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            most_exclusions_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling shortest1_insert
 */
Solution shortest1_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tshortest1" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            shortest1_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Build a solution  by repeatedly calling shortest2_insert
 */
Solution shortest2_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tshortest2" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            shortest2_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

Solution most_expensive_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tmost_expensive" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            most_expensive_insert(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}

/*
 * Added in this candidate.
 *
 * most_expensive_construct() inserts n interventions, and for every insertion it
 * rebuilds the "cheapest start time" estimate of a whole batch of candidates
 * (COST_BATCH * n of them), i.e. O(n^2 * tmax * delta * scenarios) work.  On the
 * large instances that is 1e10+ operations and it consumes the complete time
 * budget before the ALNS loop starts, so the search never runs.
 *
 * This construction keeps the same greedy idea (act on the most expensive
 * interventions first) but evaluates each intervention's cheapest start time
 * exactly once, right before it is inserted: O(n * tmax * delta * scenarios),
 * four orders of magnitude cheaper here, and it leaves the budget to the ALNS.
 */
Solution ordered_cheapest_construct(Instance *instance, std::default_random_engine *engine) {
#if VERBOSE_CONS
    cout << "construction {" << endl << "\tordered_cheapest" << endl << "}" << endl;
#endif
    Solution s(instance, engine);
    vector<uint_t> order;
    order.reserve(instance->get_interventions().size());
    const auto &avg_costs = instance->get_avg_costs();
    for (auto it = avg_costs.rbegin(); it != avg_costs.rend(); ++it) order.push_back(it->first);
    for (uint_t i : instance->get_interventions()) {
        if (find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    }
    for (uint_t i : order) {
        if (!s.has_unscheduled()) break;
        if (s.unscheduled.find(i) == s.unscheduled.end()) continue;
        if (stop()) { fixed_order_insert(s); break; }
        auto p = get_cheapest_time(s, i, 0.0);
        s.schedule(i, get<0>(p));
    }
    while (s.has_unscheduled()) fixed_order_insert(s);
    return s;
}

Solution random_with_me_violations_construct(Instance *instance, std::default_random_engine *engine) {
    Solution s(instance, engine);
    while (s.has_unscheduled()) {
        if (!stop()) {
            random_with_me_violations(s);
        } else {
            fixed_order_insert(s);
        }
    }
    return s;
}
