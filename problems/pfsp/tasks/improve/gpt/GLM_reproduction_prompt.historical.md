# GLM Reproduction Guide: Training, Testing, and Evolution Prompts for IG and EAX

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

This document transcribes the completed IG / EAX evolution experiments into a reproducible GLM experimental protocol. The objective is to have GLM propose algorithmic improvements under the **same data isolation, time budgets, parallel resources, and evaluation metrics**, rather than letting it exploit known test results to pick versions.

In this document, a "version" refers to one complete candidate algorithm, and "one full evaluation" means that the candidate finishes running on all instances and all specified random repeats in the training set and passes verification. Training results may only be used to design the next version; testing is allowed only after the final version is selected.

## General Rules

- Server: `cpu3` (`<SSH_HOST>`), actual hostname `<EXPERIMENT_HOST>`.
- Available parallel resources: at most 400 single-threaded solver processes; numerical library threads fixed to 1 so that no single solver process occupies multiple CPU cores.
- Evaluation metric:

  \[
  \operatorname{gap}(\%)=100\times\frac{L-B}{B},
  \]

  where `L` is the obtained makespan (PFSP) or tour length (TSP), and `B` is the accompanying UB / BKS for that instance. For each instance, first compute the mean gap across 10 independent runs, then take the **equal-weight average** across instances; do not weight by city count, job count, or run count.
- Every run must verify solution validity and independently recompute the objective value. Failures, timeouts, or invalid results must be retained and explicitly reported, never silently imputed or dropped from the mean.
- During the evolution phase, reading, collecting statistics on, or running the test set is strictly prohibited, as is using any existing test results to guide design choices. Only training instances, training results, and the fixed base implementation are accessible.
- Each candidate version must regenerate complete source code from the common frozen base; copying, patching, importing, or loading source code, binaries, or historical solutions from prior candidate versions is prohibited. **Summary feedback and decision records** from completed training evaluations may be used to design the next version.
- Propose only one candidate with a clear hypothesis per round; complete full training evaluation and audit the results before proposing the next round based on feedback. Do not pre-design and queue remaining versions in batch beforehand.

## PFSP: IG Evolution Experiment

### Task and Directories

- Problem: Permutation Flow Shop Scheduling Problem (PFSP).
- Local working directory: `<LOCAL_WORKSPACE>/IGEvo/EvolveHistory/V1_260909`.
- `cpu3` working directory: `/data/IGEvo/EvolveHistory/V1_260909`.
- Fixed base utility: `pfsp_math.py`, containing only pure Python PFSP objective evaluation, recurrence, insertion, and basic neighborhood operators; no candidate search strategy should be placed in it.
- Candidate directory: `history/`; training instance directory: `training/`; per-round evaluation results: `evaluations/`.

### Data Split

| Purpose | Source and Scale | Instances | Accessible During Evolution |
|---|---|---:|---|
| Training set | VRF Large, `n∈{100,200}`, `m∈{20,40,60}`, 10 instances per size | 60 | Yes |
| Strict test set | VRF Large, `n∈{300,400,500,600,700,800}`, `m∈{20,40,60}`, 10 instances per size | 180 | No |
| Small dataset | VRF Small | Excluded from this split | Cannot substitute for training |

The training set has 6 size cells: `100×20`, `100×40`, `100×60`, `200×20`, `200×40`, `200×60`, with 10 instances per cell. Reference values for gap are taken from the UB column in the accompanying `VRF_Instances/BestSolutionsAndBounds.xlsx`. GLM only receives the processing time matrix of permitted training instances, a deterministic random seed, and the run's time budget; the solver function must not receive instance names, UB, test data, solutions from other algorithms, or test results.

### Training Evaluation Configuration

| Item | Setting |
|---|---|
| Per-run budget | `T = n_jobs × n_machines × 0.03` seconds |
| Repeats per instance | 10, using the same deterministic paired seeds across candidates |
| One full evaluation | `60 × 10 = 600` solves |
| Max parallelism | 400 single-threaded processes |
| Algorithm language | Python; bypassing this via C/C++, JIT, Numba, etc. is prohibited |
| Normal budget stop | Algorithm stops internally before approx. `0.99T`; outer `1.5T + 10s` serves only as a fault watchdog |
| Candidate budget | 100 full training evaluations in total; original IG control counts as round 1 |
| Ranking | Equal-weight mean gap across the 60 instances |

Every successful solve must verify: the returned object is a permutation of all jobs; independent recomputation of makespan; consistency of gap with UB; and correctness of seeds, instance coverage, and source code hashes. Only candidates with all 600 solves succeeding are eligible for best-version ranking.

### IG Test Configuration

Only the final selected version may be run on the 180 strict test instances above. Testing retains: 10 runs per instance, `0.03nm` seconds, at most 400 single-threaded processes, and the same solution validity / makespan verification. Test reporting should at least provide per-instance mean gap, best gap, standard deviation, BKS/UB hit count, average runtime, and the 180-instance equal-weight average.

Historical training result: 100 full training evaluations, 60,000 valid solves; best was evaluation #99 (source `history/v098.py`), with a training mean gap of `1.296465%`. This is an adaptive training-selection result and must not be treated as a test-set performance estimate.

## TSP: EAX Evolution Experiment

### Task and Directories

- Problem: Euclidean TSP; EAX is the Large-variant base implementation of GA-EAX.
- Local working directory: `<LOCAL_WORKSPACE>/CodexEvo/EAXEvo`.
- `cpu3` working directory: `/data/CodexEvo/EAXEvo`.
- Frozen base implementation: `history/frozen_base/`; each candidate forms a complete, independently compilable C/C++ source directory under `history/vNNN/source/`, preserving `build.sh`, `build.log`, configuration, results, and explicit edit logs.
- BKS is used to compute gap and **must not be used as input to algorithm termination, instance classification, or search decisions**.

### Data Split

| Purpose | Scale | Instances | Notes |
|---|---|---:|---|
| Training set | Accompanying instances with `5000 ≤ n ≤ 20000` | 33 | 6 Easy, 27 Medium; explicitly excludes `usa13509` |
| Strict test set | Accompanying instances with `20000 < n ≤ 50000` | 21 | Does not participate in training or version selection |
| Historical extended benchmark | `10000 ≤ n ≤ 50000` | 37 listed items | Includes 15 training-overlap instances and `usa13509`; cannot be called a strict test set as a whole |

`usa13509` exhibited tour-length verification anomalies in historical batch benchmarks. Therefore, it does not participate in EAX training and must not be silently included in test averages; if evaluated at all, its run status and independently recomputed verification results must be listed separately.

### EAX Training Evaluation Configuration

| Item | Setting |
|---|---|
| Total budget for `5000 ≤ n ≤ 10000` | 900 seconds |
| Total budget for `10000 < n ≤ 20000` | 1200 seconds |
| Total budget definition | Timing starts before instance loading and distance preprocessing, covering population initialization, all searches, all restarts, and intermediate outputs |
| Internal restarts | At most 5 restarts after the initial search (at most 6 searches total); all searches share the same total budget |
| Repeats per instance | 10, seeds `1111`–`1120` |
| One full evaluation | `33 × 10 = 330` runs |
| Candidate budget | 50 full training evaluations in total; `v001` same-budget baseline counts as round 1 |
| Primary training metric | Mean of final best-so-far gap over 10 runs per instance, then equal-weight average across the 33 instances |

Under total-budget mode, the RNG is initialized only once per outer repeat, and internal restarts do not reset to the same random seed; loading a fixed initial population or other historical solutions is also prohibited. Solver output must include global best-so-far convergence records, the best tour across restarts, number of searches launched, final length, and cumulative elapsed time. Auditing must verify seeds, instance hashes, total budget, search count ≤ 6, tour validity as a permutation, and that the independently recomputed Euclidean integer tour length equals the recorded length.

### EAX Strict Test Configuration

To match the completed baseline test exactly, GLM's final selected version should use the following settings on the 21 strict `20000 < n ≤ 50000` instances:

| Item | Setting |
|---|---|
| Total budget per independent run | 3600 seconds |
| Internal restarts | At most 10 restarts after the initial search (at most 11 searches total); all searches share 3600 seconds |
| Repeats per instance | 10, seeds `1111`–`1120` |
| Parallelism | At most 400 single-threaded solver processes |
| Result metrics | Per-instance mean gap, best gap, standard deviation, BKS hits / 10, average elapsed time; 21-instance equal-weight average |

Historically, a 37-item benchmark over `10000–50000` was also conducted: 15 of them belong to the training set, 21 are strictly unseen instances, and the 10 runs on `usa13509` are invalid. Reporting must present training-overlap, strictly unseen, and invalid instances separately, never conflating them into a single "test-set average."

Historical training result: 50 full training evaluations, 16,500 valid runs; best was `v026`, with a training mean gap of `0.003480829%`. Its selection was based on the training set and must not be interpreted as a test-set generalization conclusion.

## General Evolution Prompt for GLM

The text below is not a verbatim transcript of a historical conversation, but a **reproduction prompt directly usable with GLM**, organized from the executed protocol. Simply replace paths with the actual paths accessible to GLM.

```text
You are responsible for improving a combinatorial optimization algorithm under a fixed computational budget. Your objective is to minimize the equal-weight mean gap (%) on the training set while strictly adhering to data isolation and independent-version constraints.

Workflow:
1. First read the experimental protocol, frozen base implementation, training instance manifest, and the summary results and decision records of completed training evaluations.
2. Reading, listing, collecting statistics on, running, or indirectly utilizing any test instances, test results, test distributions, or other algorithms' test-set results is prohibited.
3. Propose only one clear hypothesis at a time. State: which completed training feedback was referenced, what was observed, why this change may work, what the specific change is, and what outcome will support or refute it.
4. Regenerate a complete and independent candidate version from the common frozen base. Do not read, copy, modify, import, or load source code, binaries, weights, or solutions from previous candidates; only recorded experimental conclusions may be used. Candidates must not make search decisions based on instance names, BKS/UB, or test information.
5. After completing necessary synthetic correctness checks, launch one and only one full training evaluation. Retain all intermediate results and failures; never secretly rerun, delete failures, or substitute partial tests for a full evaluation.
6. Audit the full evaluation: coverage, seeds, budget, restart limits, solution validity, independent objective recomputation, gap calculation, and equal-weight aggregation. Only after the audit passes may you use that result to design the next version.
7. Do not list or run many proposals in batch beforehand. Always wait for full training feedback on the current candidate before deciding the next step. Stop training once the allowed number of full evaluations is exhausted, and select the version with the lowest training mean gap that passed audit.
8. Only after training selection is complete may you run strict testing on the final version; test results must never be used retrospectively to continue tuning or swap versions.
```

## IG-Specific Prompt for GLM

```text
Task: Improve Iterated Greedy (IG) for PFSP in Python.

The only accessible training set consists of the 60 VRF Large instances with n={100,200}, m={20,40,60}, 10 instances per size. Accessing VRF Large n=300–800 test instances or any of their statistics or results is strictly prohibited.

One full training evaluation must run 60 instances × 10 paired seeds per instance = 600 solves. Each solve has a soft time budget of T=n*m*0.03 seconds; at most 400 parallel processes, with 1 numerical library thread per process. Normal termination is bounded at approximately 0.99T; the outer 1.5T+10 seconds is solely a fault watchdog.

Ranking metric: mean gap over 10 solves per instance, then equal-weight average across all 60 instances. Gap is computed exclusively inside the evaluator using the accompanying training UB; the candidate solver only receives the processing time matrix, seed, and T, and must not receive instance names, UB, test data, or other algorithms' solutions.

Candidates must be independently written Python search strategies allowed to call the general arithmetic and neighborhood utilities in the fixed pfsp_math.py; C/C++, JIT, and Numba are prohibited. Save to history/, and every version must have a decision record. The full training evaluation budget cap is 100, including the original IG control. The final version may only be tested on the 180 n=300–800 instances after the budget is exhausted.
```

## EAX-Specific Prompt for GLM

```text
Task: Improve the algorithmic design of EAX on top of the frozen GA-EAX Large base implementation. Each candidate must regenerate a complete, independently compilable source directory from the common frozen_base rather than copying or modifying a previous candidate directory. BKS is allowed only for evaluating gap and is prohibited from participating in search, termination, or instance-identity branching.

The only instances permitted for training are the 33 allowed instances with 5000<=n<=20000; usa13509 is explicitly excluded. Reading or using instances, results, or distributions with n>20000 is prohibited, as is accessing usa13509.

One full training evaluation is 33 instances × 10 repeats = 330 runs with fixed seeds 1111–1120. Per-run total wall-clock budget is 900 seconds for 5000<=n<=10000 and 1200 seconds for 10000<n<=20000. The total budget starts before loading/preprocessing and includes initialization, search, restarts, and output. At most 5 restarts after the initial search (at most 6 searches total), all sharing one total budget. At most 400 single-threaded processes.

The training metric is the mean of the 10 final global best-so-far gaps per instance, equal-weight averaged over the 33 instances. Each round must preserve complete source code, build logs, configuration, instance hashes, seeds, run records, convergence logs, and audits. A total of 50 full training evaluations are allowed, with the baseline counting as round 1.

After training ends, strict testing uses only the 21 unseen instances with 20000<n<=50000; 10 runs per instance, seeds 1111–1120, total budget 3600 seconds per run, at most 10 restarts after the initial search (at most 11 searches total). Strict test results must not be used to continue designing candidates or replace the best version selected on training.
```

## Deliverables and Reporting Requirements

For each round, GLM must record: candidate number, which completed feedback it was based on, full design description, complete source code / build method, training configuration, and training mean gap. The final report must also include: the training score curve across all valid candidates, the best version, failed or invalid runs, per-instance and overall strict test statistics, and a clear distinction between training selection and test estimation.

## Evidence and Original Configuration Locations

- IG training constraints and audits: `<LOCAL_WORKSPACE>/IGEvo/EvolveHistory/V1_260909/PROTOCOL.md`, `FINAL_SUMMARY_ZH.md`, `audit_training_100.json`, `completion_structure_audit.json`.
- IG round-by-round feedback: `<LOCAL_WORKSPACE>/IGEvo/EvolveHistory/V1_260909/training_feedback.json`.
- EAX training constraints: `<LOCAL_WORKSPACE>/CodexEvo/EAXEvo/TRAINING_REQUIREMENTS.md`.
- EAX shared total-time implementation notes: `<LOCAL_WORKSPACE>/CodexEvo/EAXEvo/GA-EAX-total-budget/README.md`.
- EAX training evidence: `/data/CodexEvo/EAXEvo/history/ledger.json`, `protocol.json`, `training_manifest.json`, and each `history/vNNN/` directory.
- EAX historical 10k–50k benchmark report: `<LOCAL_WORKSPACE>/IGEvo/v026_TEST_REPORT.md`.
