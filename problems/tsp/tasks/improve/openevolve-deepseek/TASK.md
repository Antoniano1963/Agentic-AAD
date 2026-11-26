# OpenEvolve + DeepSeek: Improving GA-EAX Large

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

CPU4 working directory: `/data/CodeEvo/EAXEvo/agent_runs/improve_eax/OpenEvolve_DeepSeek_run01`. A single isolated account `openevolve_eax_run01` runs the framework and candidates. Access only this task and the necessary system environment; do not read other agents or the test set.

## Starting Point and Modification Permissions
Historical original GA-EAX Large (`history/v001`), with `population=300` and `offspring=30`, including the historical seeding, timing, convergence logging, and restart wrapper; no evolved candidates are introduced. `eax/initial_program.py` is a complete C++ text bundle: `FILES` stores the source files and `PARAMETERS` stores the parameters. The evaluator parses only literals without executing this Python container, and then compiles the C++ code.
Retain the EAX framework; modifications are permitted across all algorithmic components, including the internals of the crossover operator, initialization, local search, diversity management, selection, stage switching, and parameters. Executable arguments and output interfaces must be preserved. Modifying the evaluator, reference values, or input data is prohibited; hardcoding solutions, accessing results from other runs, or performing off-ledger training screening is prohibited.

## Training and Evaluation
- 33 `EUC_2D` training instances with 5087–19402 cities. The 18 instances with `n<=10000` get 900 seconds per run; the 15 instances with `n>10000` get 1200 seconds per run.
- 10 runs per instance with fixed seeds 1111–1120; 330 solves run in parallel, each pinned to a distinct logical CPU and single-threaded, with candidate evaluations run serially.
- At most 5 restarts (6 searches total) sharing the total budget, with input reading, initialization, and output included in timing. The external watchdog is set to budget + 60 seconds, retaining the historical cleanup tolerance, and must not be used as extra search time; record all nominal timeouts.
- A fixed checker verifies permutations and independently recomputes `EUC_2D` tour lengths. BKS is used strictly for external scoring and is not passed into the search.
- A candidate is eligible only if all 330 runs are valid and the candidate remains unmodified. The primary metric is the unweighted mean across the 33 instances of each instance's 10-run mean gap; the auxiliary `best gap` is the unweighted mean across the 33 instances of each instance's best gap over 10 runs. `combined_score=-mean_gap`, and ineligible candidates receive `-1e12`.
- Same as PFSP OpenEvolve: 50 formal evaluation attempts, including the initial algorithm and candidates that fail compilation or execution. Generation failures are logged separately and must not masquerade as evaluations; resume from checkpoints to make up remaining attempts caused by generation failures without repeating the initial evaluation. Stop for inspection upon persistent infrastructure failures.
- Training only; do not automatically run the independent test set. Freeze the best source code by training mean gap, and preserve all candidates, binaries, hashes, evaluation feedback, prompts, and resource logs.

## Framework and Resources
OpenEvolve 0.4.0, using the same pinned commit and dependencies as PFSP; `deepseek-flash`, max output `65536`, `temperature=1`, `5 islands`, `parallel_evaluations=1`. API timeout remains 300 seconds. The complete C++ bundle exceeds 100k characters, so `max_code_length=400000` is set to accommodate the entire algorithm.
CPU4 quota is 450 CPUs / 750 GiB; formal evaluation uses 330 CPU slots and does not adopt the ROADEF configuration. No additional per-solve memory limit is imposed. External account-level sampling monitoring records all overhead from the framework, compilation, and solving; sampled CPU is a lower bound, and per-solve `wait4` CPU/RSS/wall-clock metrics are also retained.
The preparation phase performs only API connectivity and tiny synthetic instance checks, logged separately and not counted as formal evaluations.
