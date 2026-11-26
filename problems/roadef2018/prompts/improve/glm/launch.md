# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-05. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2018/tasks/improve/glm/.

Please read the local SSH account configuration:
<LOCAL_CREDENTIAL_FILE>

Use the host, port, username, and password inside to log in to the server, and do not output the password in replies or logs.

After logging in, read the task specification on the server:
/data/CodexEvo/ROADEF2018/agent_runs/improve_champion/GLM_isolated_run01/TASK.md

Inspect the necessary problem materials, competition rules, and source code structure according to TASK.md, and then present your understanding to me:
1. What problem is to be solved, what algorithm is to be improved, and how performance is evaluated.
2. Where the training data, source code, and checker are located.
3. The evaluation budget, per-task resource requirements, and how the 15 instances and 3 repeats are organized.
4. The directory access boundaries, resource monitoring mechanism, and violation recording rules.
5. How you plan to carry out the work, and any remaining questions you have.

At this stage, only read and understand—do not modify files, do not compile or run the solver, and do not start formal evolution. Wait for me to confirm your understanding before proceeding.
