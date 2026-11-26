# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-08. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2018/tasks/improve/deepseek/.

Please read the local SSH account configuration file:
<LOCAL_CREDENTIAL_FILE>

Use the host, port, username, and password inside to log in to the server. Do not output the password in replies or logs.

After logging in, read the task specification on the server:
/data/CodexEvo/ROADEF2018/agent_runs/improve_champion/DeepSeek_isolated_run01/TASK.md

Your experimental working directory is the directory containing TASK.md. Inspect the necessary problem materials, competition rules, and source code structure according to the document, and then present to me:

1. What problem is to be solved, what algorithm is to be improved, and how performance is evaluated.
2. Where the training data, source code, and checker are located.
3. The per-task time, CPU, and memory requirements, and how the 15 instances and 3 repeats are organized.
4. The directory access boundaries, resource monitoring mechanism, and violation recording rules.
5. How you will complete all 50 full training evaluations, along with your initial work plan.

Pay special attention: you must genuinely complete all 50 full evaluations, with 45 solver runs per round, totaling 2250 formal solver runs. Do not stop early due to a temporary lack of improvement; failed or aborted attempts must retain their records, retries must use new IDs, and you must not overwrite old records or reuse old results to pass off as a new round.

At this stage, only read and understand—do not modify files, do not compile or run the solver, and do not start formal evolution. Wait for me to confirm your understanding before proceeding.
