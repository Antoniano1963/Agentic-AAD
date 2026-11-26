# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-14. Connection details are placeholders. The experiment-specific protocol is under problems/tsp/tasks/from_scratch/kimi/.

Please begin the from-scratch TSP solver development experiment on CPU3.

First read the local account credential file to obtain the SSH address, port, username, and password:
<LOCAL_CREDENTIAL_FILE>

After logging in to the server, read the complete task instructions:
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/KIMI_run01/TASK.md

Your working directory is:
/data/CodexEvo/EAXEvo/agent_runs/from_scratch/KIMI_run01

This is a task to design and implement a TSP solver from scratch. Although the directory is under `EAXEvo`, implementing EAX is not required and no off-the-shelf EAX solver is provided. Please autonomously design, implement, and iteratively improve your algorithm based on the provided problem materials, training set, and verification tools.

Please first read `TASK.md`, the problem materials, the training configuration, and the evaluation tools, briefly explain your understanding of the task, evaluation protocol, and execution plan, and then start directly without waiting for my further confirmation.

Key requirements:
1. Confine all experimental operations to the designated working directory; do not read other experiments, test sets, or other agents' code and results.
2. Follow the provided training set and evaluation protocol: 33 instances, 10 repetitions per instance, 330 parallel solve tasks per round; single CPU per task, with time budgets set according to the training configuration.
3. Use the provided evaluation entry point and preserve all candidate source code, evaluation records, failure records, and run logs in full.
4. You must complete 50 complete valid training evaluations. Do not end early due to a temporary lack of improvement, and do not pad the budget by merely re-evaluating the same candidate; continuously propose, implement, and verify well-founded improvements.
5. Do not conduct final test-set evaluation yourself. Upon completion, deliver the best candidate, reproducible run commands, and an experimental summary.
6. Do not expose account credentials or passwords in replies, reports, or logs.

Detailed protocol rules are governed by `TASK.md`; if you discover any substantive issue in the materials or tools that impedes the experiment, report it with evidence and do not unilaterally alter the evaluation criteria.
