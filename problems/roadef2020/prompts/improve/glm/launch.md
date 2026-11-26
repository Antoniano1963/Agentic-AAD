# Launch prompt

Historical launch text supplied to the operator, 2026-10-18. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2020/tasks/improve/glm/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU3 and carry out the ROADEF2020 algorithm improvement experiment based on the existing J49 ALNS source code.

First read the local account configuration file to obtain the SSH login information:
<LOCAL_CREDENTIAL_FILE>

Server: <SSH_HOST>
Port: <SSH_PORT>
Account: glm_r20_alns_run01

Sole experiment working directory:
/data/CodexEvo/ROADEF2020/agent_runs/improve_alns/GLM_run01

After logging in, first read:
/data/CodexEvo/ROADEF2020/agent_runs/improve_alns/GLM_run01/TASK.md

This is an improvement task based on the existing ALNS. The original source code is located in initial_materials/alns_source, and an editable copy is located in src. You are allowed to modify initialization, destroy operators, repair operators, local search, acceptance criteria, constraint handling, parameters, and implementation efficiency to improve validity and solution quality. You may not introduce Gurobi, CPLEX, or other external mathematical programming solvers.

Please first read TASK.md, the official problem materials, and the source code, briefly restate the task and experimental setup, and then directly begin implementation and iteration. Specific requirements are governed by TASK.md, with key points as follows:

1. You must complete 50 full training evaluation rounds. Round 1 uses the unmodified original ALNS and counts toward the 50-round budget. Do not stop early due to premature convergence or temporary lack of improvement.
2. Each round includes C01–C15, with each instance run independently 3 times. Each batch runs 15 instances in parallel, with the three repetitions executed sequentially in three batches; the next batch may be launched only after all solves and checks of the previous batch finish, and evaluation rounds must not overlap.
3. Each solve has a total wall-clock limit of at most 900 seconds, including reading, computation, output, and exiting; reserve time for exiting. Each task is allowed at most 4 logical CPUs and 16,000,000,000 bytes of memory, including all child processes; CPU bindings within the same batch must be non-overlapping.
4. Freeze the candidate version for each round so all three batches use the same version, and do not modify the evaluated program or parameters during evaluation. Launch each repetition independently, recording and reusing a fixed seed schedule across rounds.
5. Verify with the official checker, and separately record validity, resource compliance, and official objective values. Do not delete failure records such as invalid solutions, crashes, or timeouts, and do not tamper with original inputs or the checker. When failures occur, do not pass off the mean of the remaining successful runs as a complete score.
6. Retain the best candidate source code, build and run instructions, the full 50-round ledger, per-instance results, evolution log, and REPORT.md.

All project operations, dependency installations, and temporary files are restricted to the working directory. You may use existing system compilers, interpreters, and general development tools; you may not access other experiment directories or other agents' implementations, nor introduce other complete competition solvers or external reference solutions.

Do not leak the account password in outputs or reports.

Now please read the account configuration, log in to the server, read TASK.md, restate the task, and begin the experiment.
