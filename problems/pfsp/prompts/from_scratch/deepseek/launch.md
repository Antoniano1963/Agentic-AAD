# Launch prompt

Historical launch text supplied to the operator, 2026-10-24. Connection details are placeholders. The experiment-specific protocol is under problems/pfsp/tasks/from_scratch/deepseek/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU3 and conduct the "Designing a PFSP Solver from Scratch" experiment.

Read the login credentials from the local file:
<LOCAL_CREDENTIAL_FILE>

The file contains the server address, port, username, and password. Do not display the password in responses or logs.

Server: `<SSH_HOST>`
SSH port: `<SSH_PORT>`
Account: `deepseek_pfsp_scratch_run01`

Sole experimental working directory:
`/data/CodexEvo/IGEvo/agent_runs/from_scratch/DeepSeek_run01`

First read:
`/data/CodexEvo/IGEvo/agent_runs/from_scratch/DeepSeek_run01/TASK.md`

Then read the problem specification, training manifest, checker, and evaluation entry point. After briefly stating your understanding of the task and protocol, begin development and iteration directly without waiting for my further confirmation.

Note: This is the PFSP from-scratch design experiment on CPU3, which is different from the ROADEF2020 experiment on CPU4. No solver implementation is provided here; this is not about modifying the original IG, and the algorithmic structure is up to you.

Key requirements:
1. Complete 50 rounds of full training evaluation. Each round consists of 60 instances × 10 repeats per instance, totaling 600 solves. Use the designated evaluation entry point, training data, reference values, and fixed seeds.
2. Each solve has a time limit of `0.03*n*m` seconds, covering reading input, initialization, search, output, and exit; use at most 1 logical CPU, including all child processes. At most 400 solves run in parallel, and rounds run serially. Watchdog grace time is not extra search budget; reserve margin for clean exit.
3. Develop and save files only in the designated directory. Do not access other experiments, results from other agents, or the test set, and do not obtain off-the-shelf PFSP solver code as a starting point.
4. Freeze each candidate per round, preserving source code, build instructions, executables, and `run.sh`; do not modify the candidate during evaluation. Launch long evaluations in the background, track them via status files, do not resubmit due to SSH wait timeouts, and do not overwrite historical outputs.
5. Full attempts that contain failures still count toward the 50 rounds; preserve failures without deleting or renumbering. Stop full evaluations after reaching 50 rounds; do not launch a 51st round or final test set evaluation on your own.
6. Continuously propose and test well-justified algorithmic improvements, clearly distinguishing new candidates from same-version re-evaluations, and do not portray repeated runs as new algorithmic evolution. Small-sample debugging must be accounted for separately; do not conduct off-ledger full evaluations or batch candidate screening under the guise of debugging, and use the formal evaluation budget for candidate quality comparisons.
7. Check solution validity first, then compare training mean gap and best gap. Failures must not be silently excluded; a negative gap only indicates improvement over the fixed BKR reference upper bound and does not imply proven optimality.
8. External resource monitoring is already running; you do not need to start or stop it. It is logging-based monitoring rather than hard resource isolation; abide by resource constraints and do not actively expand CPU binding. Retain the evaluator's resource records, and also record actual CPU time, wall-clock time, and peak memory for development tests.
9. Deliver the best candidate's complete source code, executables, build and run instructions, the full 50-round ledger and raw outputs, `DECISIONS.md`, `REPORT.md`, and a per-round convergence CSV. Truthfully report algorithmic evolution, validity rate, training mean/best gap, all recorded overhead, failures, and limitations.

Refer to `TASK.md` for exact requirements. If any materials or tools present blocking issues, preserve evidence and report them without unilaterally altering the experimental protocol.
