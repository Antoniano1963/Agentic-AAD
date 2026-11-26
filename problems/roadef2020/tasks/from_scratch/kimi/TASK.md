# ROADEF2020: Designing and Implementing a Solver from Scratch

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

## 1. Task

Implement a complete solver for the ROADEF2020 maintenance planning problem from scratch, and continuously improve it based on training feedback. No ALNS, winning algorithm, or any other solver source code is provided in this task. You must independently understand the problem, parse inputs, design data structures and algorithms, output solutions, and verify them with the official checker.

Sole working directory:
`/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/KIMI_run01`

All project files, dependencies, builds, runs, logs, and temporary files are restricted to this directory; you are permitted to execute existing system compilers, interpreters, and general development tools and read their required system libraries. You may not read other experiment directories, other agents' implementations, or external reference solutions, nor search for or download ready-made solvers for this problem. You may consult general programming language and library documentation, and must record any external references and dependencies. You may not use Gurobi, CPLEX, or other external mathematical programming solvers.

First read this file and the official problem materials, briefly restate the task and setup, and then begin implementation. The algorithmic direction is left to your own design.

## 2. Provided Materials

| Relative Path | Contents |
| --- | --- |
| `initial_materials/documents/Challenge_Subject.pdf` | Official problem statement, constraints, and input/output formats |
| `initial_materials/documents/Challenge_Rules.pdf` | Official rules; the budget for this experiment is governed by TASK.md |
| `initial_materials/data/train/` | C_01.json through C_15.json, 15 training instances |
| `initial_materials/checker/` | Official checker, instructions, and small format examples |
| `initial_materials/python_deps/` | Python dependencies required by the checker |
| `initial_materials/MANIFEST.json` | Checksum manifest of the provided materials |
| `tools/check_solution.py` | Official checker wrapper entry point; contains no solving algorithm |
| `CPU_TOPOLOGY.json` | CPU topology and available binding range read during environment preparation, for reference by launch scripts |

Do not modify the original inputs, the official checker, or the wrapper script, and do not tamper with the verifier to allow infeasible solutions to pass. If needed for debugging, make a separate working copy. The small official examples are only for understanding the format and are not reference solutions for the training instances.

Verify a generated solution:

```bash
python3 tools/check_solution.py --instance initial_materials/data/train/C_01.json \
  --solution outputs/C_01.txt --output-dir checks/C_01
```

A solution passes only when the wrapper entry point returns 0 and `valid=true` in the JSON output. The official script may print ERROR while still returning 0, so you must not rely solely on the original script's exit code.

## 3. Experimental Setup

Each training round: 15 instances, each run independently 3 times, for a total of 45 solves.

1. Batch 1 runs C01–C15 simultaneously, once each; wait for all solves and verifications to finish.
2. Batch 2 runs the same 15 instances in parallel; only after all finish may Batch 3 begin.
3. After Batch 3 finishes, summarize the round before proceeding to the next round.

**Each batch must run 15 instances in parallel, with the 3 repetitions executed in 3 separate batches; do not run instances serially one by one, run all 45 simultaneously, or overlap rounds.** Freeze the source code, program, and parameters for each round so all three batches use the same version; do not modify the evaluated files during evaluation. Each run must start independently without warm-starting from a previous solution or search state. For randomized algorithms, use different seeds for the 3 repetitions and reuse a fixed seed schedule across rounds.

Requirements for a single solve:

- Total wall-clock time at most 900 seconds, including reading input, algorithm execution, writing output, and exiting. Reserve internal time for exiting, and the launch script must terminate all solver child processes at the 900-second deadline. Compilation and standalone checker runs do not count toward the 900 seconds, but their elapsed time must be recorded.
- At most 4 logical CPUs (2 physical cores and their SMT threads), including all child processes; bindings within the same batch must be non-overlapping, totaling 60 logical CPUs. Refer to the provided JSON for CPU topology; the isolated environment may not be able to read /proc or /sys.
- Memory at most 16,000,000,000 bytes, including all child processes. The server has a total quota of 750 GiB, which is not exclusive to this experiment; reading large JSON files and running the checker also consume memory, so checker concurrency must be controlled.
- Truthfully record timeouts, out-of-memory events, crashes, missing solutions, and invalid solutions.

Directory isolation does not equate to hard CPU and memory limits. System-level hard CPU/memory limits are not configured on this account, and external resource monitoring is arranged separately by the administrator; you must still implement run management properly.

## 4. Training Budget and Scoring Records

**You must complete and record 50 full training evaluation rounds; do not stop early due to temporary convergence or lack of improvement. Stop full evaluations after reaching 50 rounds.** The first implementation that enters full evaluation is Round 1; even if it performs poorly, you may not renumber, archive it, and reset the budget.

Compilation failures do not count toward the full round count, but must still be recorded. Once a launchable candidate has all 45 tasks attempted and recorded, it counts as one round; failures, crashes, timeouts, and invalid solutions may not be deleted from the ledger. Incomplete rounds caused by infrastructure interruptions must be clearly marked. Small-sample debugging must be recorded truthfully; do not conduct near-full-budget candidate screening off the ledger under the guise of debugging.

Report validity and resource compliance first, then report the official objective value (smaller is better). Record all 3 results per instance, and report an instance's complete mean objective and best objective only when all 3 runs are valid and compliant. Any failures must be clearly marked; do not drop failures and pass off the remainder as a complete score. For aggregate scores, specify the formula, coverage, reference values, and failure handling; do not directly compare means across different valid subsets. This is the maintenance planning objective, not the ROADEF2018 waste%.

For each round, persistently save a candidate source snapshot, binary digest, commands, seeds, timestamps, resource records, exit codes, solutions, and checker logs. Do not overwrite a single candidate directory and keep only a summary. Clearly distinguish algorithmic modifications from same-version re-evaluations.

## 5. Deliverables

Retain the best candidate source code, build instructions, general solver entry point, 50-round ledger and per-instance results, convergence records, and REPORT.md. The program must handle any instance conforming to the official format and must not output cached solutions based on filenames. The report must distinguish best observed performance from same-version repeated performance, and explain failures and limitations. Do not conduct extra evaluations outside the task scope.
