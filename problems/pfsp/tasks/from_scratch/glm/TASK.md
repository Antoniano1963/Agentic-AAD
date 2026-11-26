# GLM: Designing a PFSP Solver from Scratch

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

## Task
Implement a Permutation Flow Shop Scheduling Problem (PFSP) solver from scratch to minimize makespan, and continuously improve it through training feedback. The algorithmic structure is up to you; using IG is not required. No initial solver implementation is provided. Please read `initial_materials/PROBLEM.md`, `tools/check_solution.py`, and the training configuration before developing your algorithm.

## Scope of Operations
Sole experimental working directory: `/data/CodexEvo/IGEvo/agent_runs/from_scratch/GLM_run01`. Develop, compile, evaluate, and save files exclusively within this directory; system runtime libraries may be used for compilation and execution. You must not access other experiments, results from other agents, or hidden test data, nor obtain off-the-shelf PFSP solver code as a starting point. Training reference values are for scoring only; you must not hardcode solutions or lookup/policy tables per instance. The final algorithm must be able to handle unseen instances of the same class.

## Experimental Setup
- Training set: 60 VFR instances in `training/`; `n=100, 200`, `m=20, 40, 60`, with 10 instances per size class. The full list, input hashes, reference values, and fixed seeds are in `training_manifest.json`.
- Each full evaluation consists of 60 instances × 10 repeats, totaling 600 solves. You must complete 50 full evaluation rounds, not 100 rounds. Completed rounds containing solver failures still count toward the 50 rounds and must not be deleted or renumbered; compilation failures where no full evaluation was launched do not count as a round. For interrupted rounds, preserve the workspace state and request recovery; do not discard and rerun them on your own.
- Each solve has a time limit of `0.03*n*m` seconds, including reading input, initialization, and writing the solution; this corresponds to 60/120/180/120/240/360 seconds respectively. Reserve margin in advance for writing output.
- Each solve may use at most 1 logical CPU, including all child processes. The evaluation entry point runs at most 400 solves in parallel, scheduling the 600 solves via a queue; different active tasks use disjoint CPUs. Rounds must run serially; during a full evaluation, do not run parallel debugging, compilation, or launch another round.
- Follow the timing setup of the existing IG experiments. The historical anomaly watchdog is `1.5*budget+10` seconds, used solely to handle hangs and not as an extended search budget. Exceeding the nominal budget is recorded separately and ultimately judged during experimental auditing.
- The server's allocated memory cap is 750 GB; pay attention to total memory usage under 400-way concurrency. No new per-solve memory limit is imposed for this task; do not exhaust server memory.

## Implementation and Evaluation Interface
Develop in `src/` and freeze each complete candidate into `candidates/<version>/`. Retain source code, build commands, executables, and `run.sh`; candidates must not depend on mutable source code outside their candidate directory.

`run.sh` must accept `--instance PATH --output PATH --time-limit SECONDS --seed INT` and exit with code 0 on success. Using `exec` to launch the actual solver is recommended. Output must be JSON: `{"permutation": [0,1,...]}`, with job IDs starting from 0; the optional `makespan` field must match the checker's recomputation. Support absolute paths, as candidates are copied into an evaluation snapshot directory to run; write any temporary files to the current working directory.

Compile the candidate first and grant execute permission to `run.sh`. Configuration check (without running the solver):
```bash
python3 tools/evaluate_train.py --candidate candidates/v001 --id round001 --dry-run
```
For formal evaluation, launching in the background and continuously monitoring status is recommended; do not resubmit due to terminal wait timeouts:
```bash
nohup python3 tools/evaluate_train.py --candidate candidates/v001 --id round001 > logs/round001.log 2>&1 < /dev/null &
```
Read `evaluations/round001/status.json` and the final `summary.json`. Modifying the candidate during evaluation is prohibited. The entry point freezes a snapshot, records hashes, seeds, CPU pinning, wall-clock time, CPU time, peak RSS, and verification results, and maintains `evaluations/ledger.jsonl`. You must not modify training data, reference values, seeds, the evaluation protocol, or the checker to inflate scores; if you encounter tooling issues, document and report them first.

## Judging Improvements and Record-Keeping
Each run's `gap% = (makespan-BKR)/BKR*100`, lower is better. For each instance, report the mean gap and best gap across the 10 repeats, and compute the set-level metric as the equal-weight average over all 60 instances. Report a full round's aggregate score only when 600/600 solves are valid and the snapshot is consistent; failures must not be silently excluded from the mean. Preserve the reason for every failure. The independent checker verifies permutation completeness and recomputes makespan.

50 rounds is an evaluation budget, not equivalent to 50 brand-new algorithms. Continuously propose and test changes, clearly distinguishing new candidates from verbatim re-evaluations, and do not disguise massive repeated runs as algorithmic evolution; do not stop prematurely due to an early plateau. Record candidate changes, rationale, accept/reject decisions, and per-round results. Small-sample development tests must be accounted for separately (instances, seeds, budgets, results, and resource overhead); do not conduct off-ledger full evaluations or batch candidate screening under the guise of debugging.

External administrators monitor CPU, memory, processes, and CPU affinity by account UID to record the full experimental overhead. The current setup uses directory isolation and logging-based monitoring rather than hard cgroup resource isolation; you must not actively expand CPU affinity or stop monitoring. Violation records are preserved and external monitoring will not automatically abort the experiment.

## Final Deliverables
Save the best candidate's complete source code/binaries/build and run instructions, the complete 50-round ledger and raw outputs, `DECISIONS.md`, `REPORT.md`, and a per-round convergence CSV. Report validity rate, training mean/best gap, nominal timeouts, resource usage, all failures, and limitations; select the final candidate based solely on the training set, and do not run final test set evaluation yourself.
