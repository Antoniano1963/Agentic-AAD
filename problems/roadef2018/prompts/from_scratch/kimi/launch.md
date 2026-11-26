# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-13. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2018/tasks/from_scratch/kimi/.

Please begin the experiment to design and implement a ROADEF2018 solver from scratch.

First read the local account configuration file:
<LOCAL_CREDENTIAL_FILE>

Use the isolated account in the file to log in via SSH to server <SSH_HOST>, port 20002. Do not use the root account, and do not output the password in replies, logs, or code.

After logging in, first read:
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/KIMI_run01/TASK.md

Working directory:
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/KIMI_run01

Please read the task specification and the provided official problem materials, briefly restate your understanding, and then directly begin implementation and iteration without waiting for further confirmation from me.

Key requirements:
1. Implement the solver from scratch; do not obtain the champion algorithm or any other existing solver.
2. Use the provided B1–B15 training set and the official checker.
3. Run 15 instances in parallel per round, executing 3 repeats in sequential batches; different batches and different evaluation rounds must not overlap.
4. Each solver run may use at most 900 seconds, 8 logical CPUs, and 48 GB of memory, including all child processes.
5. Complete and record all 50 full training evaluation rounds according to the counting rules in TASK.md; do not stop early due to premature convergence or a temporary lack of improvement.
6. Retain the best candidate, source code, reproduction commands, per-run evaluation and failure records, and produce REPORT.md at the end.
7. The final solver must support any instance conforming to the official format and must not rely solely on B-instance identifiers or pre-saved solutions.
8. The system does not currently enforce per-task hard CPU/memory limits; actively comply with the resource requirements, truthfully record any violations, and do not modify system configurations yourself.

All project operations are restricted to the working directory above; TASK.md governs the detailed requirements.
