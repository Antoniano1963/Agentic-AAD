#include "alns.hpp"
#include "../local_search/local_search.hpp"
#include "../constructions/constructions.hpp"

void (*insertions[INSERT_COUNT])(Solution &solution) {
        [RANDOM_INSERT] = random_insert,
        [FIXED_INSERT] = fixed_order_insert,
        [CHEAPEST_INSERT] = cheapest_insert,
        [MOST_EXPENSIVE_INSERT] = most_expensive_insert,
        [LRD1_INSERT] = lrd1_insert,
        [LRD2_INSERT] = lrd2_insert,
        [HRD_INSERT] = hrd_insert,
        [LONGEST1_INSERT] = longest1_insert,
        [LONGEST2_INSERT] = longest2_insert,
        [SHORTEST1_INSERT] = shortest1_insert,
        [SHORTEST2_INSERT] = shortest2_insert,
        [MOST_EXCLUSIONS_INSERT] = most_exclusions_insert,
        [LEAST_EXCLUSIONS_INSERT] = least_exclusions_insert,
        [MOST_USED_INSERT] = most_used_insert,
        [LEAST_USED_INSERT] = least_used_insert,

        [N1_RANDOM_INSERT] = n1_random_insert,
        [N1_FIXED_INSERT] = n1_fixed_order_insert,
        [N1_CHEAPEST_INSERT] = n1_cheapest_insert,
        [N1_MOST_EXPENSIVE_INSERT] = n1_most_expensive_insert,
        [N1_LRD1_INSERT] = n1_lrd1_insert,
        [N1_HRD_INSERT] = n1_hrd_insert,
        [N1_LONGEST1_INSERT] = n1_longest1_insert,
        [N1_SHORTEST1_INSERT] = n1_shortest1_insert,
        [N1_MOST_EXCLUSIONS_INSERT] = n1_most_exclusions_insert,
        [N1_LEAST_EXCLUSIONS_INSERT] = n1_least_exclusions_insert,
        [N1_MOST_USED_INSERT] = n1_most_used_insert,
        [N1_LEAST_USED_INSERT] = n1_least_used_insert,

        [N2_CHEAPEST_INSERT] = n2_cheapest_insert,
        [N2_MOST_EXPENSIVE_INSERT] = n2_most_expensive_insert,
        [N2_LRD1_INSERT] = n2_lrd1_insert,
        [N2_HRD_INSERT] = n2_hrd_insert,
        [N2_LONGEST1_INSERT] = n2_longest1_insert,
        [N2_SHORTEST1_INSERT] = n2_shortest1_insert,
        [N2_MOST_EXCLUSIONS_INSERT] = n2_most_exclusions_insert,
        [N2_LEAST_EXCLUSIONS_INSERT] = n2_least_exclusions_insert,
        [N2_MOST_USED_INSERT] = n2_most_used_insert,
        [N2_LEAST_USED_INSERT] = n2_least_used_insert,

        [N3_CHEAPEST_INSERT] = n3_cheapest_insert,
        [N3_MOST_EXPENSIVE_INSERT] = n3_most_expensive_insert,
        [N3_LRD1_INSERT] = n3_lrd1_insert,
        [N3_HRD_INSERT] = n3_hrd_insert,
        [N3_LONGEST1_INSERT] = n3_longest1_insert,
        [N3_SHORTEST1_INSERT] = n3_shortest1_insert,
        [N3_MOST_EXCLUSIONS_INSERT] = n3_most_exclusions_insert,
        [N3_LEAST_EXCLUSIONS_INSERT] = n3_least_exclusions_insert,
        [N3_MOST_USED_INSERT] = n3_most_used_insert,
        [N3_LEAST_USED_INSERT] = n3_least_used_insert,

        [REGRET2_INSERT] = regret2_insert
};

void (*removals[REMOVE_COUNT])(Solution &solution) {
        [RANDOM_REMOVE] = random_remove,
        [CHEAPEST_REMOVE] = cheapest_remove,
        [MOST_EXPENSIVE_REMOVE] = most_expensive_remove,
        [LRD_REMOVE] = lrd_remove,
        [HRD_REMOVE] = hrd_remove,
        [LONGEST_REMOVE] = longest_remove,
        [SHORTEST_REMOVE] = shortest_remove,
        [MOST_EXCLUSIONS_REMOVE] = most_exclusions_remove,
        [LEAST_EXCLUSIONS_REMOVE] = least_exclusions_remove,
        [MOST_USED_REMOVE] = most_used_remove,
        [LEAST_USED_REMOVE] = least_used_remove,
        [VIOLATIONS_REMOVE] = violations_remove
};

Solution (*constructions[CONSTRUCTION_COUNT])(Instance *instance, std::default_random_engine *engine) {
        [RANDOM_CONSTRUCT] = random_construct,
        [LONGEST1_CONSTRUCT] = longest1_construct,
        [HRD_CONSTRUCT] = hrd_construct,
        [MOST_EXPENSIVE_CONSTRUCT] = most_expensive_construct,
        [CHEAPEST_CONSTRUCT] = cheapest_construct,
        [LRD1_CONSTRUCT] = lrd1_construct,
        [LRD2_CONSTRUCT] = lrd2_construct,
        [FIXED_CONSTRUCT] = fixed_order_construct,
        [LONGEST2_CONSTRUCT] = longest2_construct,
        [MOST_EXCLUSIONS_CONSTRUCT] = most_exclusions_construct,
        [SHORTEST1_CONSTRUCT] = shortest1_construct,
        [SHORTEST2_CONSTRUCT] = shortest2_construct
};

bool (*ls[LS_COUNT])(Solution &solution) {
        [ONE_SHIFT] = one_shift,
        [EXCL_TWO_SHIFT] = excl_two_shift,
        [RAND_TWO_SHIFT] = rand_two_shift
};


void ALNS::adjust_repair_weight(uint_t idx, double psi) {
    this->repair_weights[idx] = LAMBDA * this->repair_weights[idx] + (1 - LAMBDA) * psi;
}

void ALNS::adjust_destroy_weight(uint_t idx, double psi) {
    this->destroy_weights[idx] = LAMBDA * this->destroy_weights[idx] + (1 - LAMBDA) * psi;
}

double ALNS::compute_repair_method_probability(uint_t idx) {
    return this->repair_weights[idx] / this->repair_weights_sum;
}

double ALNS::compute_destroy_method_probability(uint_t idx) {
    return this->destroy_weights[idx] / this->destroy_weights_sum;
}

bool ALNS::iteration() {
    bool improved = false;

//    cur_sol_mutex.lock();
    Solution solution_ = cur_solution;
//    cur_sol_mutex.unlock();

    /* select destroy and repair methods (indices to vectors in ALNS) */
    uint_t d_idx = this->select_destroy_idx();
    uint_t r_idx = this->select_repair_idx();
    uint_t ub = max(1, int(ALNS_DEPTH * solution_.scheduled.size()));
    uniform_int_distribution<int> count(1, ub);

    /* destroy temporary solution with randomly selected destroy method */
    for (uint_t i = 0; i < count(alns_engine); ++i) {
        (*(this->destroy_methods[d_idx]))(solution_);
    }

    /* repair temporary solution with randomly selected repair method */
    while (solution_.has_unscheduled()) {
        if (!stop()) {
            (*(this->repair_methods[r_idx]))(solution_);
        } else {
            fixed_order_insert(solution_);
        }
    }

    /* perform local search */
    rvnd(solution_, ls_operators);

    /* Adjust omega parameters to influence weights */
    double omega_1 = 0;
    double omega_2 = 0;
    double omega_3 = 0;
    double omega_4 = 0;
    if (best_solution.extended_objective - solution_.extended_objective > ACCEPT_TOLERANCE) { // better than global best
        best_solution = solution_;
        omega_1 = OMEGA_1;
    }
    this->update_best_feasible(solution_);

//    cur_sol_mutex.lock();
    if (cur_solution.extended_objective - solution_.extended_objective > ACCEPT_TOLERANCE) { // better than current & accepted
        cur_solution = solution_;
        improved = true;
        omega_2 = OMEGA_2;
    } else if (solution_.extended_objective > cur_solution.extended_objective && !this->has_best_feasible) {
        // Feasibility-hunting phase: SA acceptance of worse solutions helps
        // escape infeasible attractors (C_14 s1111-style); disabled once a
        // valid solution exists to keep final quality (SA cost ~+0.4%).
        fitness_t delta = solution_.extended_objective - cur_solution.extended_objective;
        this->sa_w = 0.98 * this->sa_w + 0.02 * delta;
        if (this->sa_w > 0 && this->range(this->alns_engine) < pow(this->sa_p_target(), delta / this->sa_w)) {
            cur_solution = solution_;
            omega_3 = OMEGA_3;
        } else {
            omega_4 = OMEGA_4;
        }
    } else { // rejected (worse or equal once feasible, or not drawn)
        omega_4 = OMEGA_4;
    }

    this->adjust_weights(r_idx, d_idx, this->get_psi(omega_1, omega_2, omega_3, omega_4));

#if VERBOSE_ALNS
    cerr << "WEIGHT ADJUSTMENT" << endl;
    cerr << "psi: "<< this->get_psi(omega_1, omega_2, omega_3, omega_4) << endl;
    cerr << "repair weights: ";
    for (auto w:this->repair_weights) cerr << w << " ";
    cerr << endl;
    cerr << "destroy weights: ";
    for (auto w:this->destroy_weights) cerr << w << " ";
    cerr << endl;
#endif

    return improved;
}

ALNS::ALNS(Instance *instance, int seed) {
    this->instance = instance;
    this->alns_engine.seed(seed);
    this->ls_engine.seed(seed);
    this->range = uniform_real_distribution<double>(0, 1);
    methods.check_setup();
    for (int i = 0; i < INSERT_COUNT; ++i)
        if (methods.insertions[i])
            this->add_repair_method(insertions[i], insertions_labels[i]);
    for (int i = 0; i < REMOVE_COUNT; ++i)
        if (methods.removals[i])
            this->add_destroy_method(removals[i], removals_labels[i]);
    for (int i = 0; i < LS_COUNT; ++i) if (methods.ls[i]) this->add_ls_operator(ls[i], ls_labels[i]);
    this->add_construction(constructions[methods.construction], constructions_labels[methods.construction]);
    /* set initial weight for each method and add it to sum */
    this->repair_weights = vector<double>(this->repair_methods.size(), INITIAL_WEIGHT);
    this->repair_weights_sum = this->repair_methods.size() * INITIAL_WEIGHT;
    /* set initial weight for each method and add it to sum */
    this->destroy_weights = vector<double>(this->destroy_methods.size(), INITIAL_WEIGHT);
    this->destroy_weights_sum = this->destroy_methods.size() * INITIAL_WEIGHT;
    this->restarts_cnt = 0;
    this->has_best_feasible = false;
    this->sa_w = 0.0;
#if VERBOSE_CONFIG
    this->dump_methods();
#endif

}

/* Target acceptance probability for worse solutions: 20% at the start of the
 * run decaying geometrically to 1% at the time limit. */
double ALNS::sa_p_target() {
    double frac = (double)(chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - BEGIN).count()) / (double)TIME_LIMIT;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    return 0.20 * pow(0.05, frac);
}

/* Track the best valid (checker-feasible) solution seen; persist it so a late
 * crash still leaves a legal output on disk. */
void ALNS::update_best_feasible(Solution &solution) {
    if (!solution.is_valid()) return;
    if (!this->has_best_feasible || solution.final_objective < this->best_feasible.final_objective) {
        this->best_feasible = solution;
        this->has_best_feasible = true;
        if (!OUTPUT_PATH.empty()) this->best_feasible.save(OUTPUT_PATH);
    }
}

/* Restart helper: copy the given solution, randomly remove a fraction of
 * interventions and repair them with the noisy cheapest insertion. */
Solution ALNS::perturb(const Solution &solution) {
    Solution s = solution;
    uint_t n = s.scheduled.size();
    if (n == 0) return s;
    /* randomized perturbation strength per restart for diversity */
    uniform_real_distribution<double> spread(0.5, 1.5);
    uint_t removes = max(1u, (uint_t)(PERTURB_FRACTION * spread(this->alns_engine) * n));
    uniform_int_distribution<uint_t> pick(0, n - 1);
    for (uint_t k = 0; k < removes; ++k) {
        if (s.scheduled.empty()) break;
        uint_t idx = pick(this->alns_engine) % s.scheduled.size();
        auto it = s.scheduled.begin();
        advance(it, idx);
        s.unschedule(*it);
    }
    while (s.has_unscheduled()) {
        if (!stop()) n1_cheapest_insert(s);
        else fixed_order_insert(s);
    }
    return s;
}

/* Last-mile feasibility repair: find the worst resource-bound violation and
 * relocate its largest contributor to a cheaper start time; repeat. Fixes the
 * "one tiny overuse" pattern that kept C_14/C_15 runs invalid at the limit. */
bool ALNS::feasibility_repair(Solution &solution) {
    uint_t R = solution.instance->get_resources().size();
    uint_t T = solution.instance->get_horizon_num();
    bool fixed_any = false;
    int forced_moves = 0;
    for (int guard = 0; guard < 500; ++guard) {
        if (solution.is_valid()) { fixed_any = true; break; }
        /* worst violating (resource, time) */
        uint_t br = 0, bt = 0;
        fitness_t worst = NUMERIC_TOLERANCE;
        for (uint_t r = 1; r <= R && br == 0; ++r) {
            for (uint_t t = 1; t <= T; ++t) {
                fitness_t use = solution.resource_use[r][t];
                fitness_t v = use - solution.instance->u[solution.instance->get_ul_index(r, t)];
                if (v > worst) { worst = v; br = r; bt = t; break; }
                v = solution.instance->l[solution.instance->get_ul_index(r, t)] - use;
                if (v > worst) { worst = v; br = r; bt = t; break; }
            }
        }
        if (br == 0) break;  /* only exclusion violations (or none) left */
        /* contributors: interventions consuming br at bt, largest first */
        vector<pair<fitness_t, uint_t>> cands;
        for (uint_t i : solution.scheduled) {
            const uint_set_t &used = solution.instance->get_interventions_resources(i);
            if (used.find(br) == used.end()) continue;
            uint_t st = solution.get_start_time(i);
            uint_t d = solution.instance->delta[solution.instance->get_delta_index(i, st)];
            if (bt < st || bt >= st + d) continue;
            fitness_t w = solution.instance->r[solution.instance->get_r_index(i, st, br, bt)];
            if (w > NUMERIC_TOLERANCE) cands.emplace_back(w, i);
        }
        sort(cands.begin(), cands.end(), [](const pair<fitness_t, uint_t> &a, const pair<fitness_t, uint_t> &b) { return a.first > b.first; });
        bool improved = false;
        uint_t tries = min<size_t>(cands.size(), 12);
        uint_t stuck_i = 0; uint_t stuck_st = 0;
        for (uint_t k = 0; k < tries && !improved; ++k) {
            uint_t i = cands[k].second;
            uint_t st = solution.get_start_time(i);
            solution.unschedule(i);
            uint_t best_t = st;
            fitness_t best_c = solution.extended_objective;
            for (uint_t t = 1; t <= solution.instance->get_t_max(i); ++t) {
                fitness_t c = solution.estimate_schedule(i, t).extended_objective;
                if (c < best_c - NUMERIC_TOLERANCE) { best_c = c; best_t = t; }
            }
            if (best_t != st) {
                solution.schedule(i, best_t);
                improved = true;
            } else {
                if (k == 0) { stuck_i = i; stuck_st = st; }
                solution.schedule(i, st);  /* revert */
            }
        }
        if (!improved) {
            /* chained fallback: force the largest contributor to its best other
             * position so subsequent guard rounds can resolve the shift */
            if (forced_moves < 30 && stuck_i != 0) {
                ++forced_moves;
                solution.unschedule(stuck_i);
                uint_t best_t = stuck_st;
                fitness_t best_c = numeric_limits<fitness_t>::max();
                for (uint_t t = 1; t <= solution.instance->get_t_max(stuck_i); ++t) {
                    if (t == stuck_st) continue;
                    fitness_t c = solution.estimate_schedule(stuck_i, t).extended_objective;
                    if (c < best_c) { best_c = c; best_t = t; }
                }
                if (best_t != stuck_st) {
                    solution.schedule(stuck_i, best_t);
                    continue;
                }
                solution.schedule(stuck_i, stuck_st);
            }
            break;
        }
        fixed_any = true;
    }
    return fixed_any && solution.is_valid();
}

void ALNS::adjust_weights(uint_t repair_idx, uint_t destroy_idx, double psi) {
    this->repair_weights_sum -= this->repair_weights[repair_idx];
    this->adjust_repair_weight(repair_idx, psi);
    this->repair_weights_sum += this->repair_weights[repair_idx];
    this->destroy_weights_sum -= this->destroy_weights[destroy_idx];
    this->adjust_destroy_weight(destroy_idx, psi);
    this->destroy_weights_sum += this->destroy_weights[destroy_idx];
}

double ALNS::get_psi(double omega_1, double omega_2, double omega_3, double omega_4) {
    double max = omega_1;
    if (omega_2 > max) max = omega_2;
    if (omega_3 > max) max = omega_3;
    if (omega_4 > max) max = omega_4;
    return max;
}

uint_t ALNS::select_repair_idx() {
    vector<double> thresholds(this->repair_weights.size() + 1, 0);
    /* compute probability thresholds (normalize in range <0, 1>) */
    for (uint_t idx = 1; idx < thresholds.size() - 1; ++idx) {
        thresholds[idx] = thresholds[idx - 1] + this->compute_repair_method_probability(idx - 1);
    }
    thresholds[thresholds.size() - 1] = 1;
    double rand = this->range(alns_engine); /* get rand number in <0, 1> */
    uint_t ret = 0; /* default repair method in case of selection error */
    /* compare rand number with thresholds and select index of repair method */
    for (uint_t idx = 1; idx < thresholds.size(); ++idx) {
        if (rand <= thresholds[idx]) {
            ret = idx - 1;
            break;
        }
    }
    return ret;
}

uint_t ALNS::select_destroy_idx() {
    vector<double> thresholds(this->destroy_weights.size() + 1, 0);
    /* compute probability thresholds (normalize in range <0, 1>) */
    for (uint_t idx = 1; idx < thresholds.size() - 1; ++idx) {
        thresholds[idx] = thresholds[idx - 1] + this->compute_destroy_method_probability(idx - 1);
    }
    thresholds[thresholds.size() - 1] = 1;
    double rand = this->range(alns_engine); /* get rand number in <0, 1> */
    uint_t ret = 0; /* default repair method in case of selection error */
    /* compare rand number with thresholds and select index of repair method */
    for (uint_t idx = 1; idx < thresholds.size(); ++idx) {
        if (rand <= thresholds[idx]) {
            ret = idx - 1;
            break;
        }
    }
    return ret;
}

Solution ALNS::greedy_search() {
#if VERBOSE_CONFIG
    cout << "ALNS::greedy_search" << endl;
#endif
    auto init_solution = construction(instance, &alns_engine);

#if SAVE_SOL_PROGRESS
    init_solution = random_with_me_violations_construct(instance, &alns_engine);
    string progress_output = SOL_PROGRESS_DIR + to_string(++sol_progress_cnt) + ".txt";
    cout << progress_output << endl;
    init_solution.save(progress_output);

    while(one_shift(init_solution)) {
        string progress_output = SOL_PROGRESS_DIR + to_string(++sol_progress_cnt) + ".txt";
        cout << progress_output << endl;
        init_solution.save(progress_output);
    }
#endif

//    cur_sol_mutex.lock();
    cur_solution = init_solution;
    best_solution = cur_solution;
    this->has_best_feasible = false;
    this->update_best_feasible(init_solution);
//    cur_sol_mutex.unlock();

//    thread ls_thread(&ALNS::parallel_local_search, this);

    restarts_cnt = 0;
    // SA acceptance temperature: scaled by the true objective magnitude (final
    // objective excludes penalty terms), cooled geometrically, reheated partially
    // on restart.
    accept_temperature = 0;
    uint_t iter_cnt = 0;
    chrono::steady_clock::time_point last_best_change = chrono::steady_clock::now();
    fitness_t prev_best = best_solution.extended_objective;
    ullint_t iter_total = 0;
    double first_feasible_s = -1.0;
    bool final_polish_done = false;
    chrono::steady_clock::time_point last_repair_attempt = chrono::steady_clock::now() - chrono::seconds(10);
    /* when nothing feasible has been seen, periodic restarts diversify the
     * search: large instances may otherwise never trigger the stagnation
     * restart (global best keeps creeping within every 25 s window) */
    chrono::steady_clock::time_point no_feasible_since = chrono::steady_clock::now();

    while (!stop()) {
        ++iter_total;
        double elapsed_frac = (double)(chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - BEGIN).count()) / (double)TIME_LIMIT;
        if (!this->has_best_feasible &&
            chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - no_feasible_since).count() > 120) {
            no_feasible_since = chrono::steady_clock::now();
            restarts_cnt++;
            cur_solution = this->perturb(this->best_solution);
            this->update_best_feasible(cur_solution);
        }
        /* last-mile feasibility repair while nothing valid has been seen */
        if (!this->has_best_feasible && elapsed_frac > 0.90 &&
            chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_repair_attempt).count() >= 5) {
            last_repair_attempt = chrono::steady_clock::now();
            Solution cand = best_solution;
            if (this->feasibility_repair(cand)) this->update_best_feasible(cand);
        }
        /* final full-depth polish of the incumbent in the last slice of time */
        if (this->has_best_feasible && !final_polish_done && elapsed_frac > 0.965) {
            final_polish_done = true;
            double depth_saved = ONE_SHIFT_DEPTH;
            ONE_SHIFT_DEPTH = 1.0;
            cur_solution = this->best_feasible;
            rvnd(cur_solution, ls_operators);
            this->update_best_feasible(cur_solution);
            ONE_SHIFT_DEPTH = depth_saved;
        }
        if (this->iteration()) {
            iter_cnt = 0;
#if SAVE_SOL_PROGRESS
            string progress_output = SOL_PROGRESS_DIR + to_string(++sol_progress_cnt) + ".txt";
            cout << progress_output << endl;
            best_solution.save(progress_output);
#endif
        } else {
            iter_cnt++;
        }
        if (best_solution.extended_objective < prev_best - ACCEPT_TOLERANCE) {
            prev_best = best_solution.extended_objective;
            last_best_change = chrono::steady_clock::now();
        }

#if VERBOSE_ALNS
        cerr << "ITERATION" << endl;
        cerr << "iter_cnt: " << iter_cnt << "/" << iters_max << ", restart_cnt: " << restarts_cnt << ", cur_cost: " << cur_solution.extended_objective << ", best_cost: " << best_solution.extended_objective << endl << endl;
#endif

        // Restart triggers: iteration budget (small instances) or stagnation of
        // the global best in wall time (large instances never reach ITERS_MAX)
        bool stagnated = (chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - last_best_change).count() > 1000 * RESTART_STAG_S);
        if (iter_cnt == ITERS_MAX || (stagnated && iter_cnt >= 100)) {
            iter_cnt = 0;
            last_best_change = chrono::steady_clock::now();

//            cur_sol_mutex.lock();
            restarts_cnt++;
            bool replay_init = restarts_cnt % 2 == 1;  /* alternate origins for diversity */
            if (construction_name[construction] == "random") {
                cur_solution = construction(instance, &alns_engine);
            } else if (!this->has_best_feasible) {
                cur_solution = this->perturb(this->best_solution);
                this->update_best_feasible(cur_solution);
            } else if (!replay_init) {
                // restart from a perturbation of the incumbent instead of the
                // deterministic construction (which would replay the same basin)
                cur_solution = this->perturb(this->best_feasible);
                this->update_best_feasible(cur_solution);
            } else {
                cur_solution = init_solution;
                this->update_best_feasible(cur_solution);
            }
//            cur_sol_mutex.unlock();
        }
    }

//    ls_thread.join();

//    cur_sol_mutex.lock();
    if (best_solution.extended_objective - cur_solution.extended_objective > ACCEPT_TOLERANCE) {
        best_solution = cur_solution;
    }
    this->update_best_feasible(best_solution);
//    cur_sol_mutex.unlock();

    if (this->has_best_feasible && first_feasible_s < 0) {
        first_feasible_s = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - BEGIN).count() / 1000.0;
    }
    cerr << "DIAG iters=" << iter_total << " restarts=" << restarts_cnt
         << " feasible=" << (this->has_best_feasible ? 1 : 0)
         << " best_feasible_obj=" << (this->has_best_feasible ? this->best_feasible.final_objective : -1.0)
         << " best_ext=" << best_solution.extended_objective
         << " final_T=" << chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - BEGIN).count() / 1000.0 << endl;

    // Prefer a legal solution whenever one was found: best_feasible holds the
    // lowest-final-objective valid solution seen (best_solution updates it too).
    if (this->has_best_feasible) {
        Solution out = this->best_feasible;
        out.restarts_cnt = restarts_cnt;
        return out;
    }
    best_solution.restarts_cnt = restarts_cnt;
    return best_solution;
}

void ALNS::add_repair_method(func_t method, string method_name) {
    this->repair_methods.push_back(method);
    this->repair_methods_names[method] = method_name;
}

void ALNS::add_destroy_method(func_t method, string method_name) {
    this->destroy_methods.push_back(method);
    this->destroy_methods_names[method] = method_name;
}

string ALNS::get_repair_name(func_t method) {
    return this->repair_methods_names[method];
}

string ALNS::get_destroy_name(func_t method) {
    return this->destroy_methods_names[method];
}

void ALNS::dump_methods() {
    cout << "ALNS {" << endl;
    cout << "\tconstruction [" << endl;
    cout << "\t\t" << construction_name.begin()->second << endl;
    cout << "\t]" << endl;
    cout << "\trepair [" << endl;
    for (func_to_name_t::iterator it = this->repair_methods_names.begin(); it != this->repair_methods_names.end();) {
        cout << "\t\t" << it->second;
        if (++it != this->repair_methods_names.end()) cout << ",";
        cout << endl;
    }
    cout << "\t]" << endl;
    cout << "\tdestroy [" << endl;
    for (func_to_name_t::iterator it = this->destroy_methods_names.begin(); it != this->destroy_methods_names.end();) {
        cout << "\t\t" << it->second;
        if (++it != this->destroy_methods_names.end()) cout << ",";
        cout << endl;
    }
    cout << "\t]" << endl;
    cout << "\tlocal_search [" << endl;
    for (auto it = this->ls_operators_names.begin(); it != this->ls_operators_names.end();) {
        cout << "\t\t" << it->second;
        if (++it != this->ls_operators_names.end()) cout << ",";
        cout << endl;
    }
    cout << "\t]" << endl;

    cout << "}" << endl;
}

void ALNS::add_ls_operator(operator_t op, string op_name) {
    this->ls_operators.push_back(op);
    this->ls_operators_names[op] = op_name;
}

void ALNS::add_construction(cons_t cons, string cons_name) {
    this->construction = cons;
    this->construction_name[cons] = cons_name;
}

bool ALNS::accept_solution(fitness_t cur_cost, fitness_t new_cost) {
    double p_accept = min(exp((cur_cost - new_cost) / accept_temperature), 1.0);
    discrete_distribution<int> distribution{1 - p_accept, p_accept};
    bool accept = distribution(alns_engine);
    accept_temperature = cooling_rate * accept_temperature;

#if VERBOSE_ALNS
    cerr << "ACCEPTANCE" << endl;
    cerr << "cur_cost: " << cur_cost << ", new_cost: " << new_cost << endl << "prob_accept: " << p_accept << ", accepted: " << accept << endl << "temperature: " << accept_temperature << endl;
#endif

    return accept;
}

double ALNS::init_temperature(fitness_t cur_cost, double initial_acceptance) {
    return cur_cost * (1 - initial_acceptance) / log(0.5);
}

double ALNS::init_cooling_rate(double initial_acceptance, double final_acceptance, uint_t num_iterations) {
    return pow((1 - final_acceptance) / (1 - initial_acceptance), (1.0 / num_iterations));
}

void ALNS::parallel_local_search() {
#if VERBOSE_CONFIG
    cout << "ALNS::parallel_local_search" << endl;
#endif

    while (!stop()) {
        uint_t restarts_cnt_ = restarts_cnt;
        cur_sol_mutex.lock();
        Solution solution_ = cur_solution;
        cur_sol_mutex.unlock();
        solution_.engine = &this->ls_engine;

        for (int i = 0; i < 10; i++) {
            rvnd(solution_, ls_operators);
        }

        cur_sol_mutex.lock();
        if ((cur_solution.extended_objective - solution_.extended_objective > ACCEPT_TOLERANCE) && restarts_cnt_ == restarts_cnt) {
            cur_solution = solution_;
        }
        cur_sol_mutex.unlock();
    }
}




