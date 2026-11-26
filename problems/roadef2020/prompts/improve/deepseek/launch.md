# Launch prompt

Historical launch text supplied to the operator, 2026-10-21. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2020/tasks/improve/deepseek/.

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Please log in to CPU3 and carry out the ROADEF2020 algorithm improvement experiment based on the original J49 ALNS.

First read the local login credentials:
<LOCAL_CREDENTIAL_FILE>

Server: <SSH_HOST>
SSH port: <SSH_PORT>
Account: deepseek_r20_alns_run01
Read the password from the credential file; do not output the password in replies or experiment logs.

Sole experiment working directory:
/data/CodexEvo/ROADEF2020/agent_runs/improve_alns/DeepSeek_run01

After logging in, read completely:
/data/CodexEvo/ROADEF2020/agent_runs/improve_alns/DeepSeek_run01/TASK.md

This is a task of improving the provided original J49 ALNS, not developing from scratch. Please read the official problem materials, original source code, and checker to understand the constraints, objective function, and input/output formats. You are allowed to modify any algorithmic component on top of the ALNS, but you may not use Gurobi, CPLEX, or other external mathematical programming solvers.

First briefly restate the task and experimental setup, then begin directly without waiting for further confirmation. Follow TASK.md, paying special attention to:

1. You must complete 50 full training evaluation rounds. Round 1 evaluates the unmodified original ALNS and counts toward the 50 rounds. Do not terminate early due to convergence, and stop full evaluations after reaching 50 rounds.
2. The training set is C_01–C_15. Each round runs each instance independently 3 times, for 45 runs in total: each batch runs 15 instances in parallel, and three batches are executed sequentially; the next batch may begin only after all solves and checks of the previous batch finish, and rounds must not overlap.
3. A single solve is allowed at most 900 seconds, including reading, search, output, and exiting; at most 16,000,000,000 bytes of memory and 4 logical CPUs, including all solver child processes.
4. Bind CPUs according to CPU_GROUPS.json, where each group corresponds to 2 physical cores and their 4 SMT threads, non-overlapping within the same batch. Do not substitute consecutive indices such as 0–3 for the specified groups.
5. Use the fixed three-run seed schedule 1111, 2222, 3333, and reuse it across rounds. Start each run independently without warm-starting from a previous solution or search state.
6. Freeze the source code, binary, and parameters for each round, and do not modify the candidate during evaluation. Save all commands, seeds, results, timestamps, resource records, and official checker logs.
7. Truthfully record failures, invalid solutions, timeouts, and crashes in the ledger without deleting or renumbering. Small-sample debugging must also be recorded; do not conduct near-full-budget off-ledger screening under the guise of debugging.
8. Do not modify the original training data, official checker, or verification wrapper. Report validity rate and resource compliance first, then compare official objective values; do not delete failed results and pass off the remainder as a complete score.
9. Work strictly within the specified directory; do not read other agents' implementations or results, and do not introduce other ready-made competition solvers.
10. Finally retain the best candidate source code, build and run instructions, the 50-round ledger, per-instance results, convergence records, and REPORT.md, clearly distinguishing algorithmic improvements from same-version re-evaluations.

Account isolation has been configured, but this does not mean that CPU, memory, and time are automatically hard-limited by the system. Please properly implement run management according to TASK.md.
