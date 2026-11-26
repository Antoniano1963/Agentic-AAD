# Launch prompt

Historical launch text supplied to the operator, 2026-10-23. Connection details are placeholders. The experiment-specific protocol is under problems/pfsp/tasks/from_scratch/kimi/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU3 and conduct the "Designing a PFSP Solver from Scratch" experiment.

Read the login credentials from the local file:
<LOCAL_CREDENTIAL_FILE>

The file contains the server address, port, username, and password. Do not display the password in responses or logs.

Server: `<SSH_HOST>`
SSH port: `<SSH_PORT>`
Account: `kimi_pfsp_scratch_run01`

Sole experimental working directory:
`/data/CodexEvo/IGEvo/agent_runs/from_scratch/KIMI_run01`

First read:
`/data/CodexEvo/IGEvo/agent_runs/from_scratch/KIMI_run01/TASK.md`

Then read the problem specification, training manifest, checker, and evaluation entry point to verify the protocol. After briefly stating your understanding of the task, begin implementation and iteration directly without waiting for my further confirmation.

This is designing a PFSP solver from scratch, not modifying an existing IG. No initial solver code is provided, and the algorithmic structure is up to you.

Key requirements:
1. Complete 50 rounds of full training evaluation. Each round consists of 60 instances × 10 repeats per instance, totaling 600 solves; strictly use the fixed seeds and the designated evaluation entry point.
2. Each solve has a time limit of `0.03*n*m` seconds and may use at most 1 logical CPU; at most 400 solves run in parallel, and rounds run serially. Watchdog grace time is not extra search time.
3. Develop and save files only in the designated working directory. Do not access other experiments, results from other agents, or the test set, and do not obtain off-the-shelf PFSP solver code as a starting point.
4. Freeze each candidate per round, preserving source code, build instructions, and executables; do not modify the candidate during evaluation. Launch long evaluations in the background, track them via status files, and do not resubmit due to terminal wait timeouts.
5. Continuously try well-justified algorithmic improvements, clearly distinguishing new-candidate evaluations from verbatim re-evaluations, and do not portray repeated runs as new algorithmic evolution. Truthfully account for failures and development tests without deleting or hiding them.
6. External resource monitoring is already running; you do not need to start or stop it. Abide by resource constraints and do not interfere with monitoring.
7. Upon completion, save the best candidate, all evaluation records, `DECISIONS.md`, `REPORT.md`, and the raw convergence curve CSV; report validity rate, training mean gap/best gap, and resource overhead. Do not conduct final test set evaluation on your own.

Refer to `TASK.md` for exact requirements. If you discover blocking issues in the materials or tools, preserve evidence and report them without unilaterally altering the experimental protocol.
