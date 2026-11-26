# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-11-14. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2022/tasks/improve/deepseek/.

Please log in to CPU4 and start the ROADEF2022 S46–HiGHS V6 algorithm improvement experiment.

First read the local credential file:
<LOCAL_CREDENTIAL_FILE>

This file contains the server address, SSH port, account, password, and working directory. Do not output the password in replies or logs.

Server: <SSH_HOST>
SSH Port: <SSH_PORT>
Account: deepseek_r22_s46_run01

Sole experimental working directory:
/data/CodeEvo/ROADEF2022/agent_runs/improve_s46/DeepSeek_run01

After logging in, first read completely:
/data/CodeEvo/ROADEF2022/agent_runs/improve_s46/DeepSeek_run01/TASK.md

Then read the problem materials, original V6 source code, build instructions, training manifest, and evaluation entry point specified therein. After understanding the task, briefly restate the configuration, and then begin the experiments directly without waiting for further confirmation from me.

This task is to improve the algorithm based on the provided S46–HiGHS V6, not to develop from scratch. You are permitted to modify any part of the algorithm and implementation, not limited to parameter tuning. All development, compilation, temporary files, solves, and deliverables must be placed inside the working directory above; do not access other agents' artifacts, other experiment directories, or hidden test data.

Core requirements:
1. Complete all 50 formal training evaluation rounds, with each round covering the full set C of 50 instances, run once per instance. Do not terminate early upon hitting a plateau; re-evaluations must be explicitly marked.
2. Strictly use the unified evaluation entry point specified in TASK.md. In each round, first run the 25 long-budget instances in parallel, and only after all of them complete, run the 25 short-budget instances; rounds must not overlap.
3. The external per-instance budget is 1500 or 900 seconds as specified in the manifest, with at most 8 logical CPUs and total memory across all child processes not exceeding 30 GB. Strictly follow CPU_GROUPS.json.
4. Long evaluations should be run in the background while continuously checking existing processes and logs; do not resubmit due to SSH wait timeouts.
5. Crashes, infeasible solutions, and timeouts during started formal evaluations are all retained and counted toward rounds, without deleting, overwriting, or renumbering. Development tests are logged separately in dev/ledger.jsonl; do not circumvent the budget through off-ledger full-batch evaluations.
6. Select one complete, reproducible final candidate. Follow the feasibility and quality selection rules in TASK.md, do not stitch together per-instance best results across rounds, and do not perform hidden test-set evaluation yourself.
7. Save the complete ledger, frozen candidates and hashes, raw solutions, official checker logs, per-round convergence data, DECISIONS.md, REPORT.md, as well as the source code, run entry point, and build instructions of the best candidate. Clearly distinguish actual algorithmic improvements, repeated measurements, failures, and unverified inferences.

Resource monitoring has already been started by the researchers outside the account; you do not need to deploy or stop it yourself. setup_validation/ is validation from the environment setup stage and does not count toward your 50 formal evaluation rounds.
