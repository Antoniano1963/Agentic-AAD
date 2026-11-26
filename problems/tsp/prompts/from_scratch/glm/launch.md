# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-13. Connection details are placeholders. The experiment-specific protocol is under problems/tsp/tasks/from_scratch/glm/.

Please begin the from-scratch TSP algorithm design and evolution experiment on `cpu3`, intended for comparison with previous EAX-improvement methods. You must design the solver yourself; using EAX is not required, and no off-the-shelf EAX implementation is provided.

Read the local account configuration:
<LOCAL_CREDENTIAL_FILE>

Log in using the dedicated account provided therein:
Server: <SSH_HOST>
SSH Port: <SSH_PORT>
Account: glm_tsp_scratch_run01

Do not use `root`, and do not output passwords in replies, logs, or code.

After logging in, first read:
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/GLM_run01/TASK.md

Your sole project working directory:
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/GLM_run01

Please read the task instructions and `initial_materials/PROBLEM.md`, briefly restate the requirements, and then immediately begin implementation and iteration without waiting for further confirmation.

Key requirements:
1. Design a TSP solver from scratch; do not obtain off-the-shelf solver implementations such as EAX, LKH, or Concorde, or reference tours.
2. Use the provided 33 training instances, 10 runs per instance, with fixed seeds 1111–1120.
3. Each solve uses a single CPU and single thread; the budget is 900 seconds for `n≤10000` and 1200 seconds for the rest. Initialization, preprocessing, search, and restarts share the budget.
4. At most 5 restarts after the initial search. BKS is solely for evaluator scoring and must not be used for solver decisions or early stopping.
5. Run complete evaluations through the provided evaluation entry point, with 330 solves per round; different rounds must not overlap.
6. Complete all 50 valid complete training evaluations as specified in `TASK.md`, retaining failed attempts and their costs; do not stop on your own due to early convergence.
7. Preserve the best source code, build and run commands, complete evaluation records, `DECISIONS.md`, and `REPORT.md`.

Note the distinction between experiments:
You are responsible for the from-scratch TSP experiment on `cpu3`.
KIMI's ROADEF2018 experiment on `cpu4` is unrelated to you—do not access it, and do not adopt its 8-CPU, 3-repetition, 48 GB configuration.

A dedicated account and directory isolation have been configured, while external resource monitoring has not yet been enabled; do not mistake account isolation for full resource-limit enforcement. Exact rules are governed by `TASK.md`.
