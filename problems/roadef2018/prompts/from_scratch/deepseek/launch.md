# Launch prompt

> **Translation note:** This file is an English translation of the original Chinese launch prompt provided to the LLM agent during the experiments.

Historical launch text supplied to the operator, 2026-10-16. Connection details are placeholders. The experiment-specific protocol is under problems/roadef2018/tasks/from_scratch/deepseek/.

Please log in to CPU4 and start the experiment to design and implement a ROADEF2018 solver from scratch.

First read the local account configuration file to obtain the server address, SSH port, username, and password:
<LOCAL_CREDENTIAL_FILE>

Server: <SSH_HOST>
SSH Port: <SSH_PORT>
Account: deepseek_r18_scratch_run01

After logging in, first read the task specification:
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/DeepSeek_run01/TASK.md

Your sole experimental working directory is:
/data/CodeEvo/ROADEF2018/agent_runs/from_scratch/DeepSeek_run01

Please read TASK.md and the provided official problem materials first, briefly restate your understanding of the task, inputs/outputs, experimental budget, and resource constraints, and then directly begin implementation and iteration.

This is a task to develop a complete solver from scratch; no champion algorithm is provided. You must not search for, download, or read the champion source code, other existing solvers, other agents' implementations, or external reference solutions for the training instances. The official problem materials, training set, checker, and format examples are already placed in the working directory.

You must follow TASK.md:
1. Complete and record 50 full training evaluation rounds; do not stop early due to premature convergence or a temporary lack of improvement.
2. Each round includes B1–B15, with each instance run independently 3 times. Run 15 instances in parallel per batch, executing the three repeats sequentially as three separate batches; do not start the next batch until all solving and verification in the previous batch have finished.
3. Each solver run may use at most 900 seconds of total wall-clock time, including reading, computation, output, and exit; reserve time for exit. Use at most 8 logical CPUs and 48,000,000,000 bytes of memory, including all child processes. CPU bindings for tasks within the same batch must be mutually non-overlapping.
4. Freeze the candidate version before each round, use the same version across all three batches, and do not modify the program or parameters under evaluation during the run.
5. Truthfully record invalid solutions, crashes, timeouts, and resource violations; do not delete failure records or tamper with the inputs or checker. Follow TASK.md for exact counting and scoring rules.
6. Retain the best candidate's source code, build and run instructions, all training records, and REPORT.md at the end. The program must be able to handle any instance conforming to the official format.

All project operations and dependency installations are restricted to the working directory. You may use system-installed compilers, interpreters, and general development tools. Do not leak account credentials in outputs or reports.

Now please read the account configuration, log in to the server, read TASK.md, restate the task, and begin the experiment.
