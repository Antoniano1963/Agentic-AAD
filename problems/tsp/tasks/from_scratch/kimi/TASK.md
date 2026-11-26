# KIMI: Designing a TSP Solver from Scratch

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Server: `<SSH_HOST>` (cpu3). Dedicated account: `kimi_tsp_scratch_run01`. Log in using the provided account configuration; do not use `root`.

## 1. Task and Scope of Work

On the provided Euclidean TSP training set, implement a solver from scratch and continuously improve it to minimize the unweighted mean gap across the training set as much as possible. This task uses the same training data and evaluation budget as the previous "Improve EAX" experiments, but no EAX or other off-the-shelf solver is provided, and there is no requirement to use EAX, genetic algorithms, or specific population parameters.

Sole project working directory:

```text
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/KIMI_run01
```

All project files, source code, installed dependencies, logs, and temporary files must reside inside this directory. You may execute system development tools and read system libraries required for their execution, but you must not browse other experiment directories, historical agent results, or out-of-training data, nor modify system configurations. Do not obtain existing TSP solvers, EAX/LKH/Concorde implementations, known tours, or training reference solutions. You may consult documentation for programming languages, general-purpose libraries, and file formats, and record any external reference sources.

At the start, read the task and problem specifications, restate the experimental requirements, and then independently design, implement, and validate your algorithm. The official start time is subject to the experiment administrator's instruction; directory preparation and tool self-checks do not mean the formal experiment has started.

## 2. Provided Materials

| Relative Path | Purpose |
| --- | --- |
| `initial_materials/PROBLEM.md` | Problem definition, EUC_2D distance, and input/output formats |
| `training/` | 33 original `.tsp` training instances |
| `training_manifest.json` | Names, sizes, categories, time budgets, and input hashes; excludes BKS |
| `training_config.json` | Fixed seeds, time budgets, and concurrency configuration |
| `tools/check_tour.py` | Independent tour validity and tour length verification |
| `tools/evaluate_train.py` | Entry point for one complete training evaluation round |
| `tools/evaluation_reference.json` | BKS used exclusively by the evaluator to compute gap; must not be used for algorithmic decisions |
| `tools/selftest/` | Tiny synthetic format examples; contains no solving algorithm |

Do not modify the training data, manifest, scoring reference, checker, or evaluation scripts. If you believe there is a bug in the evaluation infrastructure, preserve evidence and report it rather than modifying the evaluation programs to improve scores.

## 3. Fixed Experimental Setup

- 33 training instances with actual sizes ranging from 5087 to 19402 cities, all symmetric Euclidean TSP (`EUC_2D`); 6 Easy and 27 Medium, excluding `usa13509`.
- 10 runs per instance with fixed seeds 1111–1120; all candidates use the same seed schedule.
- For the 18 instances with n≤10000, the total budget per run is 900 seconds; for the 15 instances with n>10000, the total budget per run is 1200 seconds.
- Timing starts before reading the input data and includes parsing, preprocessing, initialization, search, restarts, and output; time-consuming preprocessing must not be moved outside the budget.
- Each solve runs on a single CPU, single thread, in an independent process and working directory. One round comprises 33×10=330 solves at 330 concurrency, with each task pinned to a distinct logical CPU. Do not overlap different complete evaluation rounds. Server quota is 400 cores and 750 GiB; before launching, the administrator must confirm resource availability, and you must not contend for CPUs already occupied by other large-scale experiments.
- At most 5 global restarts after the initial search, i.e., at most 6 searches sharing the total time budget; a restart does not grant an additional budget. If restarts are not used, record 1 search.
- Different seed runs must not share prior solutions or search state. An incumbent may be retained within a single run, but outputs from other runs must not be read to warm-start the current solve.
- The 60-second post-budget cleanup tolerance from the historical protocol is retained, but it must not be intentionally used as extra search budget; tasks exceeding budget+60 seconds are terminated by the watchdog and marked invalid.
- This task does not preset a 48 GB per-task memory limit; you must control memory usage yourself to ensure 330 concurrency is feasible. Do not adopt the ROADEF configuration (8 CPUs / 3 repetitions / 48 GB).

## 4. Solver Interface

Under `candidates/<version>/`, retain the source code, build instructions, and compiled binary, and provide an executable `run.sh`. Compilation must be completed prior to the full evaluation.

The entry point must accept:

```text
run.sh --instance <abs_.tsp_path> --output <abs_.tour_path> --time-limit <seconds>
       --seed <integer> --max-restarts 5 --metadata <abs_JSON_path>
```

`run.sh` should locate the executable relative to its own directory and must not assume the current working directory is the source directory. See `initial_materials/PROBLEM.md` for the output tour format. The metadata JSON must at minimum contain:

```json
{"searches_started": 1}
```

This number must truthfully reflect the actual number of searches started in that run (1–6). It is also recommended to record the timestamp of each restart and the progression of the best objective value over time for auditability. Implement timing, random seeding, and the solving algorithm yourself, and do not pass BKS into the search program. The checker scores tours independently; outputting only a claimed objective value is not allowed.

Candidates within this directory may be iteratively improved step by step; freeze each version before a full evaluation and do not modify any file in that version during evaluation. Build artifacts stay inside the candidate directory, while runtime outputs must be written to paths specified by the evaluator so multiple seeds do not race to write the same file.

## 5. Evaluation and Budget

Check a single tour:

```bash
python3 tools/check_tour.py --instance training/fqm5087.tsp --tour outputs/example.tour
```

Before using the formal entry point for the first time, you may inspect the expanded task list without running the solver:

```bash
python3 tools/evaluate_train.py --candidate candidates/v001 --id eval001 --dry-run
```

Launch a complete training evaluation round:

```bash
nohup python3 -u tools/evaluate_train.py \
  --candidate candidates/v001 --id eval001 \
  > logs/eval001.log 2>&1 < /dev/null &
```

Check progress via `evaluations/eval001/status.json` and obtain final results from `summary.json`. Do not resubmit due to SSH timeouts; use a new evaluation ID each time and never overwrite an existing directory. Incomplete rounds must be inspected and handled explicitly rather than deleted and silently rerun.

The target is **50 complete valid training evaluations**; the first evaluation is your initial self-implemented version and counts as evaluation #1. A candidate is `eligible` only if all 330/330 runs pass and the version remains unmodified; build failures, runtime failures, invalid tours, or incomplete evaluations must preserve their records and resource costs and do not count toward the valid evaluation total. Stop launching new complete training evaluations once 50 valid evaluations are reached; do not skip the remaining valid evaluation budget due to early convergence.

Basic format debugging and small-scale debugging must be logged; do not repeatedly perform near-full-budget candidate screening off the ledger under the guise of debugging. Failures not deducting from the valid evaluation count do not mean they are cost-free: the time spent on every attempt, partial evaluation, build, and debugging run must be preserved.

## 6. Metrics and Deliverables

`gap% = (valid tour length − BKS) / BKS × 100%`, where lower is better. BKS is for scoring only and must not be used as input for early stopping, instance classification, or search strategy decisions.

Primary metric: average the 10 final gaps for each instance, then take the unweighted mean across all 33 instances. Auxiliary metrics: per-instance best gap (minimum over 10 runs), overall mean of best gaps, validity rate, runtime, and resource consumption. Invalid runs must not be dropped to report a complete score; select the best version by overall mean gap among `eligible` candidates.

Preserve the best version's source code, build commands, run entry point, all evaluation records, `DECISIONS.md`, and `REPORT.md`. The best version must be able to handle any format-compliant instance and must not depend on training instance filenames or pre-stored tours. The final report should document the algorithm design, the rounds at which a valid solution and a full-training-set valid evaluation were first achieved, as well as improvements and failures at each stage.

An independent login account and chroot directory isolation have been configured; you can read and write this workspace, system development tools are read-only, and other experiment directories are inaccessible. External resource monitoring has not yet been enabled, and the account itself does not enforce cumulative CPU budgets or per-task hard memory limits. The current evaluation script provides single-CPU pinning and timeout termination; `searches_started` is self-reported by candidates and must not be claimed as an algorithmic behavior independently verified by the system. Please record everything truthfully and abide by all protocols.
