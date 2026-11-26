# ROADEF2018: Designing and Implementing a Solver from Scratch

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

## 1. Task

You need to implement a ROADEF2018 solver from scratch and continuously improve its solution quality through training evaluations. This task does not provide the champion algorithm or any other existing solver. You must independently understand the problem, parse the data, design data structures and algorithms, generate solutions in the official format, and verify the results using the provided official checker.

Working directory:

```text
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/GLM_run01
```

All reading, creation, and modification of project files, as well as compilation, execution, logs, and temporary files, are restricted to this working directory. You are allowed to execute system-installed compilers, interpreters, and general development tools, and read the system libraries required for their execution; you must not read or modify other experiment directories or user data, and must not modify system configurations or install dependencies outside the working directory. Any additional dependencies must be installed within this directory.

This directory already contains the original materials required for this task. Do not search for, download, or read the champion source code for this problem, other complete solvers, or other agents' implementations, and do not use external reference solutions for the training instances. You may consult documentation for programming languages and general-purpose libraries; record any external references and dependencies actually used. The official A0 example included with the checker is solely for understanding the input and output formats.

At the start, read this document and the official problem description, briefly restate the task, constraints, and experimental setup, and then begin implementation. No specific algorithmic direction is prescribed; design and validate your approach independently.

## 2. Problem Materials and Tools

All paths below are relative to the working directory:

| File or Directory | Purpose |
| --- | --- |
| `initial_materials/documents/problem.pdf` | Official problem, constraint, and format specification |
| `initial_materials/documents/rules.pdf` | Official competition rules; the execution budget for this task is governed by this document |
| `initial_materials/data/train/dataset_B/` | B1–B15, 15 training instances in total, each containing batch and defects files |
| `initial_materials/data/common/global_param.csv` | Global problem parameters |
| `initial_materials/checker/readme.txt` | Official checker usage instructions |
| `initial_materials/checker/bin/checker` | Precompiled official checker |
| `initial_materials/checker/src/`, `include/` | Official checker source code |
| `initial_materials/checker/instances_checker/` | Official A0 input and solution format example |
| `initial_materials/checker/visualization/checker.html` | Official solution visualization tool |
| `tools/check_solution.py` | Convenient verification wrapper; does not provide a solving algorithm |

Please keep the original instances, global parameters, official checker, and verification wrapper script unmodified; if needed for debugging, copy them into a working subdirectory. Do not modify the checker or input data to make invalid solutions pass.

To verify a B1 solution you generated:

```bash
cd /data/CodeEvo/ROADEF2018/agent_runs/from_scratch/GLM_run01
python3 tools/check_solution.py \
  --instance initial_materials/data/train/dataset_B/B1 \
  --solution outputs/B1_solution.csv \
  --output-dir checks
```

`--solution` points to the file you generated. `valid: true` in the JSON output and an exit code of 0 from the script indicate that official verification passed; full logs are stored in the returned `run_directory`. Invalid solutions may cause the official checker to crash or time out; the wrapper script sets a 60-second guard timeout for the checker, which does not count toward the solver's 900-second budget.

## 3. Experimental Setup

Absolute path to the training set:

```text
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/GLM_run01/initial_materials/data/train/dataset_B
```

Each complete training evaluation round consists of 15 instances, with each instance run 3 times, for a total of 45 solver runs. The execution order must be:

```text
Batch 1: Launch B1–B15 simultaneously, running each once
Wait for all solving and verification in Batch 1 to finish
Batch 2: Launch B1–B15 simultaneously, running each once
Wait for all solving and verification in Batch 2 to finish
Batch 3: Launch B1–B15 simultaneously, running each once
Wait for all of Batch 3 to finish, then summarize the round
```

**You must run 15 instances in parallel per batch and execute the three repeats in separate sequential batches. Do not run instances serially one by one, do not launch all 45 solver runs at once, and do not overlap different evaluation rounds.** Freeze the candidate code before each round begins; all three batches must use the same version, and the source code, executable, or parameters under evaluation must not be modified during the run.

Requirements for each solver run (one run of one instance):

- External wall-clock time of at most **900 seconds**, including data reading, algorithmic computation, output writing, and process exit of the solver process; the internal algorithm should reserve time for output and exit. Compilation and separately executed official checking do not count toward this 900-second limit, though their overhead should be recorded.
- At most **8 logical CPUs**, including all child processes; the 15 tasks in the same batch must use mutually non-overlapping CPU bindings, totaling at most 120 logical CPUs. The limit is on available CPUs, not merely requiring the thread count to be at most 8.
- At most **48 GB of memory, i.e., 48,000,000,000 bytes**, including all child processes. The server's total memory quota is 750 GiB; during parallel execution, you must still monitor total memory usage and currently available resources, and not assume all memory is exclusive to this task.
- Do not use solutions or search states from a previous repeat to warm-start a subsequent run; launch the program independently each time. If stochastic mechanisms are used, record the actual seeds; within the same round, use three distinct seeds for the three runs of each instance, and reuse the same seed schedule across subsequent rounds.

These are experimental protocol requirements and do not imply that system-level hard CPU or memory limits are enforced. External account isolation and resource monitoring are configured separately by the experiment administrator; you are still responsible for properly binding CPUs and managing solving time and resources in your own launch scripts. Timeouts, memory overruns, or execution failures must be recorded truthfully and must not be deleted or disguised as compliant results.

## 4. Training Evaluation Budget and Result Recording

**You must complete and record 50 full training evaluation rounds; do not stop early due to premature convergence or a temporary lack of improvement.** Each full evaluation round consists of the 15 × 3 solver runs described above; stop adding new full training evaluations once 50 rounds have been reached.

Compilation failures do not count toward the number of full evaluation rounds, but their records must be retained. Once a launchable candidate enters a full evaluation, any individual run that crashes, times out, or produces no valid solution must be recorded as a failure; as long as all 45 tasks in the round have been attempted and their results recorded, it counts as one round. Do not remove a round from the ledger due to poor performance or invalid solutions. Basic debugging and small-sample runs must not be passed off as full evaluations, nor may you repeatedly conduct near-full-budget candidate screening off the ledger under the guise of debugging. Rounds interrupted by infrastructure failures must retain all records and be clearly marked.

Prioritize official validity first, then compare solution quality. For each round, report per-instance validity across the three runs, raw waste, mean waste%, and best waste%; when all three runs are valid, mean is the average of the three runs and best is the minimum of the three. Only when all instances complete three valid runs should you report the equally weighted overall mean across all 15 instances. Whenever any invalid run occurs, mark it prominently; never drop failed results and report the mean of the remaining runs as a complete score.

`waste% = waste / (waste + total_item_area) × 100%`, where smaller is better. This is not the gap relative to BKR. For every run, retain the source code version or SHA-256 hash, exact command, seed, start and end times, exit code, output solution, and checker log. Retain at least the rounds corresponding to the first valid solution, the first all-instances-valid round, and every historical best candidate.

## 5. Final Deliverables to Retain

Retain in this working directory the best candidate's source code, build instructions, solver entry point, 50-round training records, and the final `REPORT.md`, ensuring that solutions can be generated from input instances and re-verified following the instructions. The best program must be able to handle any instance conforming to the official format and must not simply output pre-saved solutions based on B1–B15 filenames.

The recommended unified solver entry point is:

```text
./run.sh --instance <input_prefix> --params <global_param_file> --output <solution_file> --time-limit <seconds> --seed <integer>
```

You need to implement this entry point and the solving algorithm yourself. If another interface is used, you must provide clear, copy-pasteable run commands. In the report, truthfully distinguish between validity, optimization gains, stochastic variance, and resource violations, and do not substitute unverified speculation for results.
