# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-14. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2018/tasks/from_scratch/glm/.

Please log in to CPU4 and start the experiment to design and implement a ROADEF2018 solver from scratch.

First read the local login configuration:
<LOCAL_CREDENTIAL_FILE>

Use the account and password inside to log in to the server via SSH:
<SSH_HOST>, port 20002
Account: glm_r18_scratch_run01

Do not output the password in replies or logs.

Server working directory:
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/GLM_run01

After logging in, first read completely:
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/GLM_run01/TASK.md

Then read the official problem specification, training data, and checker usage instructions specified therein. First briefly restate your understanding of the task, constraints, evaluation metrics, and budget, and then immediately begin implementation and experiments without waiting for further confirmation from me.

This is a ROADEF2018 from-scratch development task on CPU4, unrelated to the TSP experiment on CPU3. You need to implement data parsing, data structures, solving algorithms, and official-format output yourself. Do not read or use the champion source code, existing solvers, other agents' implementations, or external reference solutions.

Strictly follow TASK.md, paying special attention to:
1. All project operations are restricted to the specified working directory; do not modify the training data or the official checker.
2. You must complete and record 50 full training evaluation rounds; do not stop early due to temporary convergence.
3. Each round includes B1–B15, with each instance run 3 times, totaling 45 solver runs. Run 15 instances in parallel per batch, execute the three batches sequentially, and do not overlap rounds.
4. Each single run of an instance may use at most 900 seconds of wall-clock time, 8 logical CPUs, and 48 GB of memory, including all child processes. Properly manage these resources yourself in accordance with TASK.md.
5. Freeze the candidate version before each round, use the same code and parameters across all three repeats, and do not reuse solutions or search states across runs.
6. Truthfully save all evaluation, failure, and resource violation records. Do not delete poor results or repeatedly conduct near-full-budget candidate screening off the ledger.
7. Select the final general-purpose solver based on training results, and retain the source code, build and run entry points, complete ledger, and REPORT.md.

Once 50 rounds are completed, stop adding new full training evaluations and report back to me the final candidate path, training scores, key algorithmic changes, and actual resource consumption.
