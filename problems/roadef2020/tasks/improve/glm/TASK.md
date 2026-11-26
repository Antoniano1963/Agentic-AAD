# ROADEF2020: Improving the ALNS Solver

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

## 1. Task

Starting from the provided J49 ALNS source code, improve the solving performance on the ROADEF2020 maintenance planning problem. This implementation does not rely on Gurobi or CPLEX. You are allowed to modify any part of the algorithm, including initialization, destroy operators, repair operators, local search, acceptance criteria, constraint handling, parameters, and implementation efficiency, while preserving the task identity of improving upon the provided ALNS. You may not introduce Gurobi, CPLEX, or other external mathematical programming solvers.

Sole working directory:

```text
/data/CodexEvo/ROADEF2020/agent_runs/improve_alns/GLM_run01
```

All project files, dependencies, builds, runs, outputs, and temporary files must be placed inside this directory. You are allowed to use existing system compilers, interpreters, general tools, and required system libraries; you may not access other experiment directories, other agents' implementations, or data outside the working directory, and you may not modify system configurations. You may not search for or incorporate other complete competition solvers or external reference solutions. You may consult general development documentation, and must record any external references and dependencies.

First read this file, the official problem specification, and the original source code, briefly restate the task and setup, and then begin implementation and iteration. No improvement direction is pre-specified. The original implementation may output infeasible solutions; validity must be verified by the official checker, and you must not rely solely on the solver's own objective value or exit code.

## 2. Materials and Initial Code

The following paths are relative to the working directory:

| Path | Purpose |
| --- | --- |
| `initial_materials/documents/Challenge_Subject.pdf` | Official problem statement, constraints, and input/output formats |
| `initial_materials/documents/Challenge_Rules.pdf` | Official competition rules; the budget for this run is governed by TASK.md |
| `initial_materials/data/train/` | C_01.json through C_15.json, 15 training instances in total |
| `initial_materials/alns_source/` | Frozen original J49 ALNS source code, CMakeLists.txt, and README |
| `initial_materials/checker/` | Original official checker, instructions, and format examples |
| `initial_materials/python_deps/` | Python dependencies available for the checker |
| `initial_materials/SOURCE_MANIFEST.json` | Source provenance and SHA-256 digests of the original materials |
| `src/` | Editable copy initially identical to the original ALNS |
| `tools/check_solution.py` | Official checker wrapper entry point; reports validity and objective value |

Do not modify the original materials, training inputs, official checker, or verification wrapper script. If modifications are needed for debugging, copy them to another subdirectory, and never pass off results from a modified checker as official results.

Build and solve example:

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
OMP_NUM_THREADS=4 OMP_THREAD_LIMIT=4 taskset -c <assigned_4_logical_CPUs> \
  ./build/challengeRTE -p initial_materials/data/train/C_01.json \
  -o outputs/C_01.txt -t 850 -s 1111
python3 tools/check_solution.py \
  --instance initial_materials/data/train/C_01.json \
  --solution outputs/C_01.txt --output-dir checks/C_01
```

850 seconds is an example of reserving exit time for the original program; the external hard deadline remains 900 seconds and must be enforced by the launch script. The CPU list in the example must be allocated according to the actual server topology; do not directly use placeholder text. A solution passes only when the checker wrapper exits with code 0 and `valid=true` in the JSON output; the official script may return 0 even when printing ERROR, so you must not rely solely on its exit code.

## 3. Experimental Setup

Training set: `/data/CodexEvo/ROADEF2020/agent_runs/improve_alns/GLM_run01/initial_materials/data/train/`.

Each round has 15 instances, with each instance run independently 3 times, for a total of 45 solves. Consistent with prior experiments:

1. Batch 1 launches C01–C15 simultaneously, running each once.
2. Wait until all solves and checks finish before launching Batch 2 of 15 instances.
3. After Batch 2 finishes completely, launch Batch 3, and summarize the round once all runs finish.

**Each batch must run 15 instances in parallel; the three repetitions must be executed in separate batches. Do not run instances serially one by one, do not run all 45 together, and do not overlap different evaluation rounds.** Freeze the candidate source code, executable, and parameters for each round so all three batches use the same version; do not modify the evaluated files during evaluation. Each run must start independently without warm-starting from a previous solution or search state; when using randomized mechanisms, record three distinct seeds and reuse the same seed schedule across rounds.

Each solve (one run of one instance):

- External wall-clock time at most 900 seconds, including data reading, algorithmic computation, output, and exiting; reserve time for exiting, and terminate the entire solver process tree upon timeout. Compilation and standalone checker runs do not count toward the 900-second solve time, but their overhead must be recorded.
- At most 4 logical CPUs, corresponding to 2 physical cores and their threads under the server's SMT topology, including all child processes. The 15 tasks within the same batch must have non-overlapping bindings, totaling 60 logical CPUs. Limiting thread count alone is not sufficient.
- Memory at most 16 GB, i.e., 16,000,000,000 bytes, including all child processes. The server's total quota is 750 GiB, and actual available memory varies with other workloads; the checker also consumes memory when reading large JSON files, so checker concurrency should be limited.
- Timeouts, out-of-memory events, crashes, missing outputs, and infeasible solutions must all be truthfully preserved in the records.

This specification does not mean that system-level hard CPU and memory limits have already been configured. Account isolation and external resource monitoring are arranged separately by the administrator; you must still properly manage CPU bindings, time, and resources.

## 4. Evaluation Budget and Recording

**You must complete 50 full training evaluation rounds; do not terminate early due to premature convergence or temporary lack of improvement.** Round 1 evaluates the unmodified ALNS as the same-protocol baseline and counts toward the 50 rounds. Stop adding new full evaluations after reaching 50 rounds.

Compilation failures do not count toward the full round count, but must be recorded. Once a launchable candidate enters full evaluation, having all 45 solves attempted and recorded counts as one round; runtime crashes, timeouts, and invalid solutions may not be removed from the ledger. Incomplete rounds caused by infrastructure interruptions must be marked separately. Small-sample debugging must be recorded truthfully; do not conduct near-full-budget candidate screening off the ledger under the guise of debugging.

Report validity rate and resource compliance rate first, then compare the official objective values of valid solutions (smaller is better). Retain all three independent results per instance; report the complete mean objective and best objective for an instance only when all three runs are valid and compliant. Clearly mark instances containing failures; do not drop failed runs and present the remaining average as a complete score. When using aggregate metrics or relative gap to baseline, specify the formula, reference values, covered instances, and failure handling; different candidates must not have their means compared directly over different subsets of valid instances. This is not the ROADEF2018 waste metric, nor are competition points used as an additional training rule.

For each run, record the source version or SHA-256, command, seed, start and end times, actual wall-clock time, CPU and memory records (when obtainable), exit code, output solution, and checker logs; record failure types. Do not modify candidate files during evaluation, ensuring a one-to-one correspondence between source code and scores.

## 5. Deliverables

Retain the best candidate source code, build instructions, general solver entry point, 50-round ledger, per-instance results, evolution log, and REPORT.md. The report must explain the candidate selection rationale and discuss feasibility and solution quality separately. The program should handle any instance conforming to the official format and must not output cached solutions based solely on training filenames. Do not conduct extra evaluations outside the task scope.
