# Launch prompt

Historical launch text supplied to the operator, 2026-10-23. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2020/tasks/from_scratch/deepseek/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU4 and carry out the experiment "Designing a ROADEF2020 Solver from Scratch".

Read the login information from the local file:
<LOCAL_CREDENTIAL_FILE>

The file contains the server address, port, username, and password. Do not display the password in replies or logs.

Server: <SSH_HOST>
SSH port: <SSH_PORT>
Account: deepseek_r20_scratch_run01

Sole experiment working directory:
/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/DeepSeek_run01

First read:
/data/CodeEvo/ROADEF2020/agent_runs/from_scratch/DeepSeek_run01/TASK.md

Then read the provided problem materials, input/output formats, official checker, and CPU_GROUPS.json. After briefly stating your understanding of the task and protocol, begin development and iteration directly without waiting for further confirmation from me.

This is a task of designing a complete solver from scratch, not modifying an existing ALNS or winning algorithm. Only the problem materials, training data, and verification tools are provided initially; the algorithm is to be designed by you.

Key requirements:
1. Complete 50 full training evaluation rounds. The training set is C_01–C_15, with each instance run independently 3 times, for 45 solves per round.
2. Each round is divided into 3 batches: each batch runs 15 instances simultaneously, and the next batch may begin only after all solves and checks finish; different rounds must not overlap.
3. Each solve has a total wall-clock limit of at most 900 seconds, including reading input, initialization, search, writing output, and exiting; at most 16,000,000,000 bytes of memory and 4 logical CPUs, including all solver child processes. Bind CPUs strictly according to CPU_GROUPS.json, and reserve time for exiting.
4. Develop, compile, and save files strictly within the specified working directory. Do not access other agents' code, results, or the test set, and do not obtain ready-made solver implementations as a starting point.
5. For each round, freeze the source code, binary, and parameters, and save their hashes; do not modify the evaluated candidate during evaluation. Use an experiment-wide mutual exclusion lock and unique output paths to avoid duplicate runners and overwriting results. Launch long evaluations in the background and confirm progress through process and status files rather than resubmitting when SSH waiting ends.
6. Rounds that were fully attempted even if they contain invalid solutions, crashes, or timeouts still count toward the 50 rounds, and all failures must be preserved. Stop after completing 50 rounds; do not unilaterally add a Round 51 or a final test-set evaluation.
7. Continuously propose and test well-motivated algorithmic improvements, clearly distinguishing new candidates from same-version re-evaluations, and do not describe repeated runs as new algorithmic evolution. Small-sample tests during development must also be logged separately.
8. Report validity first, then report the official objective value. Means across different subsets of valid instances cannot be compared directly; do not delete failures and pass off the remainder as a complete score. Record all 3 results as well as mean/best objective for each instance.
9. External resource monitoring has already been started; you do not need to start or stop it. It is recording-only monitoring, not hard resource isolation; you must still adhere to the resource limits and record CPU time, wall-clock time, peak memory, seed, exit code, and check results for each run.
10. Finally deliver the complete source code of the best candidate, the executable, build and run instructions, the full 50-round ledger and raw outputs, DECISIONS.md, REPORT.md, and the raw CSV for the convergence curve. Truthfully report failures, incidents, extra tests, and limitations.

Specific requirements are governed by TASK.md. If you discover blocking issues in the materials or tools, preserve evidence and report them without unilaterally changing the experimental protocol.
