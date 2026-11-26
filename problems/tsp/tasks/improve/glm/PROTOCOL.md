# GLM Reproduction Experiment Specification: Training, Testing, and Evolution Prompts for IG and EAX

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

This document transcribes the completed IG / EAX evolution experiments into a reproducible experimental protocol for GLM. The goal is for GLM to propose algorithmic improvements under the **same data isolation, time budgets, parallel resources, and evaluation criteria**, rather than using known test results to select versions.

In this document, a "version" refers to a complete candidate algorithm; "one complete evaluation" means that the candidate finishes running on all instances in the training set across all prescribed random repetitions and passes validation. Training results may only be used to design the next version, and testing is permitted only after the final version has been selected.

## General Rules

- Server: `cpu3` (`<SSH_HOST>`), actual hostname `<EXPERIMENT_HOST>`.
- Available parallel resources: up to 400 single-threaded solver processes; numerical library thread counts must be pinned to 1 so a single solver process never occupies multiple CPU cores.
- Evaluation metric:

  \[
  \operatorname{gap}(\%)=100\times\frac{L-B}{B},
  \]

  where `L` is the obtained makespan (PFSP) or tour length (TSP), and `B` is the accompanying UB / BKS for that instance. For each instance, first compute the mean gap over 10 independent runs, and then take the **unweighted mean** across instances; do not weight by number of cities, number of jobs, or number of runs.
- Every run must undergo solution validity verification and independent objective recomputation. Failures, timeouts, or invalid results must be retained and explicitly reported; never silently impute values or omit them from the mean.
- During the evolution phase, reading, collecting statistics on, or running the test set is strictly prohibited, as is leveraging any existing test results to guide design choices. Only training instances, training results, and the fixed base implementation are accessible.
- Each candidate version must regenerate its complete source code from the shared frozen base; copying, patching, importing, or loading source code, binaries, or historical solutions from a previous candidate version is prohibited. **Summary feedback and decision records** from completed training evaluations may be used to design the next version.
- Propose only one candidate with an explicit hypothesis per round; complete the full training evaluation and audit the results before proposing the next round based on feedback. Do not pre-design and queue remaining versions in a batch beforehand.

## PFSP: IG Evolution Experiment

### Task and Directories

- Problem: Permutation Flow Shop Scheduling Problem (PFSP).
- Local working directory: `<LOCAL_WORKSPACE>/IGEvo/EvolveHistory/V1_260909`.
- `cpu3` working directory: `/data/IGEvo/EvolveHistory/V1_260909`.
- Fixed base utilities: `pfsp_math.py`, containing only pure-Python PFSP objective calculation, recurrences, insertion, and basic neighborhood operators; candidate search strategies must not be placed in it.
- Candidate directory: `history/`; training instance directory: `training/`; per-round evaluation results: `evaluations/`.

### Data Split

| Purpose | Source and Scale | Instances | Accessible During Evolution? |
|---|---|---:|---|
| Training set | VRF Large, `n∈{100,200}`, `m∈{20,40,60}`, 10 instances per size | 60 | Yes |
| Strict test set | VRF Large, `n∈{300,400,500,600,700,800}`, `m∈{20,40,60}`, 10 instances per size | 180 | No |
| Small dataset | VRF Small | Excluded from this split | No (cannot substitute for training) |

The training set comprises 6 size cells: `100×20`, `100×40`, `100×60`, `200×20`, `200×40`, `200×60`, with 10 instances per cell. Reference values for gap come from the accompanying UBs in `VRF_Instances/BestSolutionsAndBounds.xlsx`. GLM receives only the processing-time matrix of permitted training instances, deterministic random seeds, and the per-run time budget; the solver function must not receive instance names, UBs, test data, solutions from other algorithms, or test results.

### Training Evaluation Configuration

| Item | Setting |
|---|---|
| Per-run budget | `T = n_jobs × n_machines × 0.03` seconds |
| Repetitions per instance | 10, using identical deterministic paired seeds across candidates |
| One complete evaluation | `60 × 10 = 600` solves |
| Max parallelism | 400 single-threaded processes |
| Algorithm language | Python; bypassing this constraint via C/C++, JIT, Numba, etc. is prohibited |
| Normal budget stop | Stop internally before approximately `0.99T`; outer `1.5T + 10 seconds` serves only as a fault watchdog |
| Candidate budget | 100 complete training evaluations in total, with the original IG control counted as #1 |
| Ranking | Unweighted mean gap across the 60 instances |

Every successful solve must be verified: the return value is a valid permutation of all jobs; makespan is independently recomputed; gap agrees with the UB calculation; and seeds, instance coverage, and source hashes are correct. Only candidates with all 600 runs succeeding are eligible to rank for the best version.

### IG Testing Configuration

Only the finally selected version may be run on the 180 strict test instances above. Testing retains: 10 runs per instance, `0.03nm` seconds, up to 400 single-threaded processes, and the same solution validity / makespan verification. Test reporting must at minimum provide per-instance mean gap, best gap, standard deviation, BKS/UB hit count, average runtime, and the unweighted mean across all 180 instances.

Historical training outcome: 100 complete training evaluations, 60,000 valid solves; best was evaluation #99 (source `history/v098.py`) with a training mean gap of `1.296465%`. This is an adaptive training selection result and must not be interpreted as an estimate of test-set performance.

## TSP: EAX Evolution Experiment

### Task and Directories

- Problem: Euclidean TSP, with EAX based on the Large variant of GA-EAX.
- Local working directory: `<LOCAL_WORKSPACE>/CodexEvo/EAXEvo`.
- `cpu3` working directory: `/data/CodexEvo/EAXEvo`.
- Frozen base implementation: `history/frozen_base/`; each candidate forms a complete, independently compilable C/C++ source directory under `history/vNNN/source/` and retains `build.sh`, `build.log`, configuration, results, and an explicit edit log.
- BKS is used to compute gap and **must not be used as input for algorithm termination, instance classification, or search decisions**.

### Data Split

| Purpose | Scale | Instances | Notes |
|---|---|---:|---|
| Training set | Supplied instances with `5000 ≤ n ≤ 20000` | 33 | 6 Easy, 27 Medium; explicitly excludes `usa13509` |
| Strict test set | Supplied instances with `20000 < n ≤ 50000` | 21 | Not used for training or version selection |
| Historical extended evaluation | `10000 ≤ n ≤ 50000` | 37 listed entries | Includes 15 training-overlap instances and `usa13509`; cannot be called a strict test set as a whole |

`usa13509` exhibited a tour-length validation anomaly in historical batch evaluations. Therefore, it is excluded from EAX training and must not be silently folded into test averages; if evaluated at all, its execution status and independently recomputed validation results must be reported separately.

### EAX Training Evaluation Configuration

| Item | Setting |
|---|---|
| Total budget for `5000 ≤ n ≤ 10000` | 900 seconds |
| Total budget for `10000 < n ≤ 20000` | 1200 seconds |
| Meaning of total budget | Timed from before instance loading and distance preprocessing, including population initialization, all searches, all restarts, and intermediate output |
| Internal restarts | At most 5 restarts after the initial search, i.e., at most 6 searches; all searches share the same total budget |
| Repetitions per instance | 10, seeds `1111`–`1120` |
| One complete evaluation | `33 × 10 = 330` runs |
| Candidate budget | 50 complete training evaluations in total; `v001` same-budget baseline counts as #1 |
| Primary training metric | Mean of the 10 final best-so-far gaps per instance, then unweighted mean across all 33 instances |

In total-budget mode, the random number generator is initialized only once per outer repetition, and internal restarts do not reset to the same random seed; loading a fixed initial population or other historical solutions is also prohibited. Solver outputs must include the global best-so-far convergence trace, the best tour across restarts, the number of searches started, the final length, and the cumulative elapsed time. Auditing must verify seeds, instance hashes, total budget, number of searches ≤ 6, tour validity as a permutation, and that the independently recomputed integer Euclidean distance equals the recorded length.

### EAX Strict Testing Configuration

To match the completed baseline tests exactly, GLM's finally selected version must be evaluated on the strict 21 instances with `20000 < n ≤ 50000` under the following settings:

| Item | Setting |
|---|---|
| Total budget per independent run | 3600 seconds |
| Internal restarts | At most 10 after the initial search, i.e., at most 11 searches; all searches share 3600 seconds |
| Repetitions per instance | 10, seeds `1111`–`1120` |
| Parallelism | Up to 400 single-threaded solver processes |
| Result metrics | Per-instance mean gap, best gap, standard deviation, BKS hits / 10, average elapsed time; 21-instance unweighted mean |

Historically, a 37-entry evaluation for `10000–50000` was also run: 15 belong to the training set, 21 are strictly unseen instances, and all 10 runs on `usa13509` were invalid. Reporting must separate training-overlap, strictly unseen, and invalid instances, and must not mix them into a single "test set mean."

Historical training outcome: 50 complete training evaluations, 16,500 valid runs; best was `v026` with a training mean gap of `0.003480829%`. Its selection was based on the training set and must not be interpreted as a test-set generalization conclusion.

## General Evolution Prompt for GLM

The text below is not a verbatim transcript of a historical conversation, but rather a **reproduction prompt directly deliverable to GLM**, compiled from the executed protocol. Simply replace paths with the paths actually accessible to GLM.

```text
You are responsible for improving a combinatorial optimization algorithm under a fixed computational budget. Your goal is to minimize the unweighted mean gap (%) on the training set while strictly adhering to data isolation and independent version constraints.

Workflow:
1. First read the experiment protocol, frozen base implementation, training instance manifest, summary results of completed training evaluations, and decision logs.
2. You are prohibited from reading, listing, collecting statistics on, running, or indirectly leveraging any test instances, test results, test distributions, or results of other algorithms on the test set.
3. Propose only one explicit hypothesis at a time. State: which completed training feedback was referenced, what was observed, why this modification may be effective, what the exact modification is, and what results would support or refute it.
4. Regenerate a complete, independent candidate version from the shared frozen base. Do not read, copy, modify, import, or load source code, binaries, weights, or solutions from previous candidates; only documented experimental conclusions may be used. Candidates must not make search decisions based on instance names, BKS/UB, or test information.
5. After completing necessary synthetic correctness checks, launch one and only one complete training evaluation. Retain all intermediate results and failures; do not secretly rerun, delete failures, or substitute partial tests for a complete evaluation.
6. Audit the complete evaluation: coverage, seeds, budget, restart limits, solution validity, independent objective recomputation, gap computation, and unweighted aggregation. Only after the audit passes may you use those results to design the next version.
7. Do not pre-list or run many schemes in a batch. Wait for the complete training feedback of the current candidate before deciding the next step. Stop training once the allowed number of complete evaluations is exhausted, and select the version with the lowest training mean gap that passed auditing.
8. Only after training selection is complete may you run strict testing on the final version; test results must never be fed back to continue tuning or swap versions.
```

## IG-Specific Prompt for GLM

```text
Task: Improve Iterated Greedy (IG) for PFSP in Python.

The only accessible training set consists of VRF Large instances with n={100,200} and m={20,40,60}, 10 instances per size, 60 instances in total. Accessing VRF Large n=300–800 test instances or any of their statistics or results is strictly prohibited.

One complete training evaluation must run 60 instances × 10 paired seeds per instance = 600 solves. The soft time budget per run is T=n*m*0.03 seconds; up to 400 parallel processes, with numerical library thread count set to 1 per process. Normal termination occurs before approximately 0.99T; the outer 1.5T+10 seconds is used solely as a fault watchdog.

Ranking metric: mean gap across the 10 solves per instance, then unweighted mean across all 60 instances. Gap is computed only inside the evaluator using the accompanying training UB; the candidate solver receives only the processing-time matrix, seed, and T, and must not receive instance names, UB, test data, or solutions from other algorithms.

Candidates must be independently written Python search strategies and may call general arithmetic and neighborhood utilities from the fixed pfsp_math.py; C/C++, JIT, and Numba are prohibited. Save under history/, and maintain a decision log for every version. The complete training evaluation budget is capped at 100 evaluations, including the original IG control. The final version may be tested on the 180 n=300–800 instances only after the budget is exhausted.
```

## EAX-Specific Prompt for GLM

```text
Task: Improve the algorithm design of EAX on top of the frozen GA-EAX Large base implementation. Every candidate must regenerate a complete, independently compilable source directory from the shared frozen_base rather than copying or modifying a previous candidate directory. BKS is permitted only for evaluating gap and is prohibited from participating in search, stopping, or instance-identity branching.

The only accessible training instances are the 33 permitted instances with 5000<=n<=20000; usa13509 is explicitly excluded. Reading or using instances, results, or distributions with n>20000 is prohibited, as is accessing usa13509.

One complete training evaluation comprises 33 instances × 10 repetitions = 330 runs with fixed seeds 1111–1120. The total wall-clock budget per run is 900 seconds for 5000<=n<=10000 and 1200 seconds for 10000<n<=20000. The total budget starts before loading/preprocessing and includes initialization, search, restarts, and output. At most 5 restarts after the initial search (at most 6 searches total), all sharing one total budget. Up to 400 single-threaded processes.

The training metric is the unweighted mean across the 33 instances of each instance's 10-run mean final global best-so-far gap. Every round must retain complete source code, build logs, configuration, instance hashes, seeds, run records, convergence records, and audits. A total of 50 complete training evaluations are permitted, with the baseline counted as #1.

After training ends, strict testing uses only the 21 unseen instances with 20000<n<=50000: 10 runs per instance, seeds 1111–1120, 3600 seconds total budget per run, and at most 10 restarts after the initial search (at most 11 searches total). Strict test results must not be used to continue designing candidates or to replace the best version selected during training.
```

## Deliverables and Reporting Requirements

For each round, GLM must record: candidate ID, which completed feedback motivated it, complete design specification, complete source code / build method, training configuration, and training mean gap. The final report must also include: the training score curve across all valid candidates, the best version, failed or invalid runs, per-instance and overall statistics from strict testing, and a clear distinction between training selection and test estimation.

## Evidence and Original Configuration Locations

- IG training constraints and audits: `<LOCAL_WORKSPACE>/IGEvo/EvolveHistory/V1_260909/PROTOCOL.md`, `FINAL_SUMMARY_ZH.md`, `audit_training_100.json`, `completion_structure_audit.json`.
- IG round-by-round feedback: `<LOCAL_WORKSPACE>/IGEvo/EvolveHistory/V1_260909/training_feedback.json`.
- EAX training constraints: `<LOCAL_WORKSPACE>/CodexEvo/EAXEvo/TRAINING_REQUIREMENTS.md`.
- EAX shared total-budget implementation notes: `<LOCAL_WORKSPACE>/CodexEvo/EAXEvo/GA-EAX-total-budget/README.md`.
- EAX training evidence: `/data/CodexEvo/EAXEvo/history/ledger.json`, `protocol.json`, `training_manifest.json`, and each `history/vNNN/` directory.
- EAX historical 10k–50k comparison report: `<LOCAL_WORKSPACE>/IGEvo/v026_TEST_REPORT.md`.
