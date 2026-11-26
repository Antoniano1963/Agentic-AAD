# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Archived launch file. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2022/tasks/from_scratch/deepseek/.

Please log in to CPU4 and conduct the "Design a ROADEF2022 Solver from Scratch" experiment.

Read the login information from the local file:
<LOCAL_CREDENTIAL_FILE>

The file contains the server address, port, username, and password. Do not display the password in replies or logs.

Server: <SSH_HOST>
SSH Port: <SSH_PORT>
Account: deepseek_r22_scratch_run01

Sole experimental working directory:
/data/CodeEvo/ROADEF2022/agent_runs/from_scratch/DeepSeek_run01

First read:
/data/CodeEvo/ROADEF2022/agent_runs/from_scratch/DeepSeek_run01/TASK.md

Then read the problem specification, training manifest, checker, and evaluation entry point, and verify the protocol. After briefly stating your understanding of the task, begin implementation and iteration directly without waiting for further confirmation from me.

This is designing a ROADEF2022 solver from scratch, not modifying the existing S46. No initial solver code is provided; the algorithmic structure is up to you.

Key requirements:
1. Complete 50 full training evaluation rounds. Each round covers 50 instances, run once per instance, for 50 solves in total; strictly use the fixed seed and the specified evaluation entry point.
2. Each solve is time-limited to 900 or 1500 seconds as specified in the training manifest, with at most 8 logical CPUs and total memory across all child processes not exceeding 30 GB. Strictly bind according to CPU_GROUPS.json; in each round, first run the 25 long-budget instances in parallel, and only after all of them finish, run the 25 short-budget instances in parallel; run different rounds sequentially. Watchdog grace time is not additional search time.
3. Develop and save files only in the designated working directory. Do not access other experiments, other agents' results, or the test set, and do not obtain existing ROADEF2022 solver code as a starting point.
4. Freeze the candidate for each round, saving the source code, build instructions, and executable; do not modify the candidate during evaluation. Launch long evaluations in the background and track them via status files; do not resubmit due to terminal wait timeouts.
5. Continuously pursue principled algorithmic improvements, clearly distinguish new candidate evaluations from unchanged re-evaluations, and do not describe repeated runs as new algorithmic evolution. Log failures and development tests honestly without deleting or concealing them.
6. External resource monitoring has already been started; you do not need to start or stop it yourself. Respect resource constraints and do not interfere with monitoring.
7. Upon completion, save the best candidate, all evaluation records, DECISIONS.md, REPORT.md, and raw CSV files for the convergence curve, and report the feasibility rate, per-instance objective values, training mean gap, and resource usage. Do not conduct final test-set evaluation yourself.

Specific requirements are governed by TASK.md. If you discover blocking issues in the materials or tools, preserve evidence and report them without altering the experimental protocol on your own.
