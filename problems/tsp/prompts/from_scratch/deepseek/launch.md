# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-20. Connection details are placeholders. The experiment-specific protocol is under problems/tsp/tasks/from_scratch/deepseek/.

Please log in to CPU3 to conduct the experiment on designing and implementing a TSP solver from scratch.

First read the local account credential file to obtain the SSH login information:
<LOCAL_CREDENTIAL_FILE>

Server: <SSH_HOST>
Port: <SSH_PORT>
Account: deepseek_tsp_scratch_run01

Sole experiment working directory:
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/DeepSeek_run01

After logging in, first read:
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/DeepSeek_run01/TASK.md

This is a task to develop a complete TSP solver from scratch. Although the directory path contains `EAXEvo`, no EAX or other off-the-shelf solver source code is provided, and there is no requirement to use EAX, genetic algorithms, or specified population parameters. Please parse the inputs, design data structures and algorithms, generate tours, and verify them using the provided checker on your own.

Please first read `TASK.md`, `initial_materials/PROBLEM.md`, the training configuration, and the evaluation entry point, briefly restate the task and experimental setup, and then begin implementation. Specific requirements are governed by `TASK.md`, with key points summarized below:

1. The training set consists of 33 instances, with 10 independent runs per instance using fixed seeds 1111–1120. Each round runs 330 solves at 330 concurrency, with each task pinned to a distinct logical CPU; each solve runs on a single CPU, single thread. Do not overlap complete evaluation rounds.
2. Instances with ≤10000 cities get 900 seconds per run; those with >10000 cities get 1200 seconds per run. The budget covers reading, preprocessing, initialization, search, restarts, and output. The 60-second cleanup tolerance must not be intentionally used for additional search.
3. At most 5 global restarts after the initial search (at most 6 searches total), sharing the total time budget; truthfully record `searches_started`. Different runs must not share solutions or search state.
4. You must complete 50 complete valid training evaluations; do not stop early due to early convergence or temporary lack of improvement. Eligibility is determined by the provided evaluator; all failed or incomplete evaluations and debugging costs must be preserved without deleting, overwriting, or resetting records after archiving.
5. Use `tools/evaluate_train.py` as the complete evaluation entry point. Run a dry-run first; launch formal evaluations in the background and monitor `status.json` continuously—do not resubmit due to SSH wait timeouts.
6. Freeze candidate source code, binaries, and parameters before each evaluation round and do not modify them during the run. The candidate entry point must implement the `run.sh` argument interface specified in `TASK.md`.
7. The primary metric is the unweighted mean across all 33 instances of each instance's 10-run mean gap. BKS is used exclusively by the evaluator for scoring and must not be used as input for search strategies, instance classification, or early stopping.
8. Preserve the best candidate's source code, build and run instructions, all evaluation records, `DECISIONS.md`, and `REPORT.md`, distinguishing among algorithmic modifications, same-version re-evaluations, and best observations.

Do not obtain existing TSP solvers, EAX/LKH/Concorde implementations, known tours, training reference solutions, or results from other agents. You may consult documentation for programming languages, general-purpose libraries, and file formats, and record your sources. Do not modify the training data, scoring reference, checker, or evaluation scripts.

All project operations, dependency installations, logs, and temporary files are restricted to the working directory. You may use existing system compilers, interpreters, and general development tools. This task does not preset a ROADEF-style per-task memory limit; you must manage total memory usage to support 330 concurrency.

Directory preparation does not mean compute resources have been reserved. Before starting a 330-concurrency complete evaluation, obtain confirmation from the experiment administrator regarding CPU3 resource availability; while waiting, you may read materials, develop, and run small-scale self-checks.

Do not expose account credentials or passwords in any output or report. Please now read the account configuration, log in to the server, read `TASK.md`, restate the task, and begin development.
