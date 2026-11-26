# Launch prompt

Historical launch text supplied to the operator, 2026-10-21. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2020/tasks/from_scratch/glm/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU4 via SSH and carry out the experiment of designing a ROADEF2020 solver from scratch.

Please read the login information from the local file:
<LOCAL_CREDENTIAL_FILE>

Server: <SSH_HOST>
Port: <SSH_PORT>
Account: glm_r20_scratch_run01

Sole experiment working directory:
/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/GLM_run01

After logging in, first read completely:
/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/GLM_run01/TASK.md

This is a task of designing and implementing a complete solver from scratch, not improving an existing ALNS. Please read the provided official problem materials, training data, and checker on your own to understand the constraints, objective function, and input/output formats.

First briefly restate the task and experimental setup, then begin directly without waiting for further confirmation. Strictly follow TASK.md, completing and recording all 50 full training evaluation rounds—do not stop early due to convergence, and do not exceed 50 rounds.

Pay special attention to the following:
1. Each round runs 15 instances, 3 times each: 15 instances run in parallel per batch, with three batches executed sequentially; do not overlap batches or rounds.
2. Each solve is allowed at most 900 seconds, 16 GB of memory, and 4 logical CPUs, including all solver child processes.
3. You must use the CPU groups in CPU_GROUPS.json, where each group corresponds to 2 physical cores and their SMT threads. Do not substitute consecutive indices such as 0–3 on your own.
4. Freeze the source code, binary, and parameters for each round; truthfully record all failures, invalid solutions, timeouts, and small-sample debugging, and never delete or reset the ledger.
5. Conduct the experiment strictly within the specified working directory; do not read other agents' implementations or results, and do not use ready-made solvers or external mathematical programming solvers.
6. Save complete per-round records, per-instance results, official checker logs, candidate source code, and build instructions, and finally submit REPORT.md and convergence records.

Account directory isolation does not mean that the system enforces hard CPU and memory limits; you must properly implement run management on your own according to TASK.md.
