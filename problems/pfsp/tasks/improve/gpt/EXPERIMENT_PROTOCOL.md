# IG Optimization Experiment Conventions

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

## Data Split

- Training set: all VRF Large instances with 100 and 200 jobs; 20, 40, and 60 machines; instance IDs 1–10 per size class; 60 instances in total.
- Test set: all VRF Large instances with 300–800 jobs; 180 instances in total.
- The Small dataset is excluded from this training/test split.

## Training Evaluation

- User-confirmed: each training instance is evaluated **10 times** per candidate algorithm (not 15 times).
- One full training-set evaluation for a candidate algorithm comprises `60 × 10 =` **600 solves**.
- Per-solve time budget: `n_jobs * n_machines * 0.03` seconds.
- Current convention: at most 400-way parallelism, with numerical library threads fixed to 1 per solver process.
- Gap uses the UB from the accompanying `VRF_Instances/BestSolutionsAndBounds.xlsx`.

This document records requirements for subsequent experiments and does not indicate that new training or benchmarking has already been started.

## Mandatory Constraints for This Evolution Run (`V1_260909`)

The user initially requested recording only; subsequently, explicit authorization was granted under the active objective: formally begin improvement, with at most 100 full training evaluations, terminating once the evaluation budget is exhausted. The following data isolation, independent-version, and runtime conditions remain in effect.

1. During the improvement phase, only training data may be accessed: all 60 instances with 100 and 200 jobs. Accessing test data, testing on test data, or attempting to obtain test data distributions or related statistics is prohibited. Reading historical full-benchmark files containing test-instance results to assist improvement is prohibited. Although test information appeared in earlier conversation turns and cannot be erased from prior context, it must not be used for candidate design or selection in this run.
2. Running all 60 training instances with 10 repeats each (600 solves total) is defined as **1 full evaluation**. Partial or short checks must not be counted as a full evaluation; if performed subsequently, they must be recorded separately.
3. The code for every candidate version must be written from scratch, without modifying, inheriting from, or patching previous solutions. This constraint must not be unilaterally relaxed to allow reusing the previous version's code.
4. The server working directory for this run is fixed at `/data/IGEvo/EvolveHistory/V1_260909`. All candidate version source files are stored in its `history/` subdirectory; intermediate evaluation files, logs, and results are all placed within this run's working directory.
5. Evolved IG algorithms must be implemented in Python and no other language, so as to compare algorithmic designs. Bypassing this requirement by generating implementations in other languages or compiling accelerated candidate cores is prohibited.
6. Continue adhering to the established runtime conditions: per-solve budget of `n_jobs * n_machines * 0.03` seconds, at most 400-way parallelism, and 1 numerical library thread per process.

The user has directly confirmed: "Start formally now; 100 full training evaluations are authorized. The server is cpu3." Execution is authorized within the above boundaries; 100 full training evaluations must not be exceeded.

The user further clarified: designs must proceed continuously based on actual solution evaluation feedback; fixing 39 proposals upfront and running them blindly is not acceptable. The preset "40 screening + 60 re-evaluation" schedule is revoked. Going forward, analyze full training results round by round before submitting new candidates with concrete hypotheses and decision records; existing budget is not reset, and unexecuted proposals are not evaluated automatically.

Latest supplemental authorization: finish evaluating the prior 39 proposals first as prior knowledge; thereafter, continuously improve based on feedback. Both the original IG control and the already-launched `v040` additional control count against the budget, so this phase totals 41 evaluations, leaving 59 remaining. The fixed 60-round re-evaluation plan remains canceled.
