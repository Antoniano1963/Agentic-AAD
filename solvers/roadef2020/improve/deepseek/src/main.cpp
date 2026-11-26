#include <iostream>
#include <string>
#include <chrono>
#include "util.hpp"
#include "parser/parser.hpp"
#include "parser/fast_parser.hpp"
#include "instance/instance.hpp"
#include "solution/solution.hpp"
#include "constructions/constructions.hpp"
#include "removals/removals.hpp"
#include "alns/alns.hpp"
#include "params.hpp"
#include <thread>
#include <future>
#include <omp.h>


using namespace std;

int main(int argc, char *argv[]) {
    /* check if any argument was passed */
    if (argc < 2) {
        cerr << "No argument passed" << endl;
        exit(1);
    }

    parse_global_params(argc, argv);

    /* parse file placed as first argument*/
    Instance instance;
    auto t_begin = chrono::steady_clock::now();
    if (FAST_PARSE) {
        FastParser fp(INPUT_PATH);
        fp.load_and_process(&instance);
        FastParser::process_avg_properties(&instance);
    } else {
        Parser *parser = new Parser(INPUT_PATH);
        parser->load();
        parser->process(&instance);
        /* free memory occupied by parser */
        delete parser;
    }
    auto t_loaded = chrono::steady_clock::now();

    /* determine dependent parameters from the instance, then let explicit
     * command-line overrides win (the reference build silently discarded them) */
    determine_dependent_params(&instance);
    parse_global_params(argc, argv);

    #if VERBOSE_CONFIG
    dump_config();
    #endif

    ALNS search(&instance, SEED);
    auto solution = search.greedy_search();
    auto t_searched = chrono::steady_clock::now();

    /* report internal state so every run can be cross-checked against the
     * official checker without guessing */
    const bool feasible = solution.is_valid();
    cerr << "RESULT"
         << " feasible=" << (feasible ? 1 : 0)
         << " objective=" << solution.final_objective
         << " mean_risk=" << solution.mean_risk
         << " excess=" << solution.expected_excess
         << " overuse=" << solution.workload_overuse
         << " underuse=" << solution.workload_underuse
         << " excl=" << solution.exclusion_penalty
         << " scheduled=" << solution.scheduled.size()
         << " unscheduled=" << solution.unscheduled.size()
         << " iterations=" << search.get_iterations_total()
         << " restarts=" << solution.restarts_cnt
         << " repair_steps=" << search.get_repair_steps_total()
         << " ms_destroy=" << (long long)search.get_ms_destroy()
         << " ms_insert=" << (long long)search.get_ms_insert()
         << " ms_rvnd=" << (long long)search.get_ms_rvnd()
         << " ms_repair=" << (long long)search.get_ms_repair()
         << " osd=" << ONE_SHIFT_DEPTH
         << " load_ms=" << chrono::duration_cast<chrono::milliseconds>(t_loaded - t_begin).count()
         << " search_ms=" << chrono::duration_cast<chrono::milliseconds>(t_searched - t_loaded).count()
         << endl;

    solution.save(OUTPUT_PATH);

    if (RETURN_ID) {
        return TEAM_ID;
    } else {
        return 0;
    }
}
