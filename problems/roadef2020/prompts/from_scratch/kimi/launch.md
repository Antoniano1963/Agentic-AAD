# Launch prompt

Historical launch text supplied to the operator, 2026-10-19. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2020/tasks/from_scratch/kimi/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU4 and carry out the experiment of designing and implementing a ROADEF2020 solver from scratch.

First read the local account configuration file to obtain the SSH login information:
<LOCAL_CREDENTIAL_FILE>

Server: <SSH_HOST>
Port: <SSH_PORT>
Account: kimi_r20_scratch_run01

Sole experiment working directory:
/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/KIMI_run01

After logging in, first read:
/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/KIMI_run01/TASK.md

This is a task of developing a complete solver from scratch. Only the official problem materials, the C01–C15 training set, the official checker, and format examples are provided in the directory; no ALNS or winning algorithm source code is provided, and the src directory is empty. You must parse the data, design data structures and algorithms, generate solutions, and verify validity on your own.

Please first read TASK.md and the official problem specification, briefly restate the task, objective function, constraints, and experimental setup, and then directly begin implementation and iteration. Specific requirements are governed by TASK.md; pay particular attention to:

1. You must complete and record 50 full training evaluation rounds. The first implementation to enter full evaluation counts as Round 1; even if it fails or performs poorly, you may not archive it and reset the budget. Do not terminate early due to temporary convergence or lack of improvement.
2. Each round consists of 15 instances, each run independently 3 times. Each batch runs 15 instances in parallel, with the three repetitions executed sequentially in three batches; the next batch may begin only after all solves and checks of the previous batch finish, and evaluation rounds must not overlap.
3. Each solve has a total wall-clock limit of at most 900 seconds, including input reading, computation, output, and exiting; reserve time for exiting and terminate all solver child processes at the 900-second deadline. Each task is allowed at most 4 logical CPUs and 16,000,000,000 bytes of memory, including all child processes. CPU bindings within the same batch must be non-overlapping; refer to CPU_TOPOLOGY.json.
4. For each round, persistently save a frozen source snapshot and executable digest; all three batches use the same version, and evaluated files must not be modified during evaluation. Each run starts independently without warm-starting from a previous solution; record three distinct seeds and reuse a fixed seed schedule across rounds.
5. Verify with the official checker, and separately record validity, resource compliance, and official objective values. Do not delete invalid solutions, crashes, or timeout records, and do not tamper with inputs or the checker. Do not pass off a mean computed after dropping failures as a complete score.
6. Retain the best candidate source code, build and general run instructions, the full 50-round ledger, per-instance results, convergence records, and REPORT.md, distinguishing algorithmic modifications, same-version re-evaluations, and best observed results.

You may not search for, download, or read ready-made solvers for this problem, other agents' implementations, or external reference solutions; you may not use Gurobi, CPLEX, or other external mathematical programming solvers. You may consult general language and library documentation, and must record the materials used.

All project operations, dependency installations, and temporary files are restricted to the working directory. You may use existing system compilers, interpreters, and general development tools. Account isolation does not mean that hard CPU and memory limits have been configured; you must still properly manage runtime resources.

Do not leak the account password in outputs or reports. Now please read the account configuration, log in to the server, read TASK.md, restate the task, and begin the experiment.
