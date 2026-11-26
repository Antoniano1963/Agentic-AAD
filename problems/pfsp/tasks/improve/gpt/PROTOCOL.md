# `V1_260909` Training Evolution Experiment

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

The user has explicitly authorized a formal start, with a cap of 100 full evaluations.

- Only 60 training instances are permitted: 100 and 200 jobs, 20, 40, and 60 machines, instance IDs 1–10.
- `prepare_training.py` copies only the 60 explicitly listed files; UB reads only rows 2–61 of the `Large` sheet corresponding to the training records without iterating over other records. Prior full-benchmark results are not read.
- One full evaluation: 60 instances × 10 runs each = 600 runs. `ledger.json` reserves evaluation numbers prior to launch, up to a maximum of 100; the original IG control is also counted.
- Per-run soft budget: `n*m*0.03` seconds; 400 processes, 1 numerical library thread per process. Normal stop threshold is 0.99× budget; the outer `1.5× + 10` seconds serves only as a fault watchdog.
- Candidates receive only the processing time matrix, random seed, and budget; instance names, UB, other solutions, or results are not provided. During solving, an audit hook prohibits file I/O, network access, and subprocesses.
- Each candidate's strategy function is written independently, without reading, modifying, inheriting from, or importing other candidate files. All candidates share the fixed `pfsp_math.py`, which contains newly written pure Python PFSP recurrence, insertion, and basic neighborhood utilities with no candidate search strategies. No C/C++, JIT, or Numba.
- `v000` is the original IG control and is not claimed as a new design; `v021` is an independently rewritten classic IG strategy control used to separate implementation-speed effects of the shared math utilities from strategy effects.
- Short correctness checks use only artificial random matrices, are logged as synthetic checks, and do not masquerade as full evaluations.
- Deterministic, cross-candidate paired training seeds are used; the same seed does not guarantee bit-for-bit reproducibility under a wall-clock budget.
- Successful results must pass full permutation verification and independent makespan recomputation. Only candidates with all 600 runs succeeding are eligible for best-candidate ranking; gap uses training UB without modifying UB.
- Candidate files reside in `history/`, inputs in `training/`, intermediate results in `evaluations/`, and all artifacts of this run remain inside this working directory.

Evaluation objective: average gap (%) over the 60 instances; each instance is averaged over its 10 solves first, then equally weighted across instances. Ranking is based strictly on training results, without using prior test information.

## Feedback-Driven Continuous Improvement (Replacing the Original Preset Queue)

The user explicitly required continuous design driven by actual results. The original fixed plan of "40 screening + 60 re-evaluations" is revoked; no automatic promotion or re-evaluation will be scheduled. Completed and in-progress evaluations retain their original numbers, and the budget is not reset.

Every new design must be accompanied by a decision record stating: which completed training evaluations are cited, what was observed, what hypothesis is proposed, what this round specifically tests, and how the next step will be decided from the results. The controller verifies that feedback references point to completed evaluations; when the queue has no newly analyzed candidate submitted, it waits rather than auto-filling the budget.

The user subsequently granted further authorization: finish testing the original 39 independent proposals first as prior knowledge, and thereafter continuously improve based on feedback. Accordingly, evaluation of the unexecuted original proposals is resumed, without restoring the fixed 60 re-evaluations. One original IG control, 39 original proposals, plus 1 already-launched `v040` control total 41 evaluations; the remaining 59 are reserved for feedback-driven improvement.

Every evaluated source file remains immutable; subsequent versions carry forward experimental insights while writing code independently under the original rules. Re-evaluations or ablation studies must have specific justifications and likewise count toward the maximum of 100 full evaluations.

The completion batch of original proposals is explicitly tagged `prior_knowledge`, citing this user authorization and the original IG control; it does not claim that these pre-written proposals were designed from newly produced feedback. After evaluation 41 finishes, the controller waits for a new analytical decision and does not automatically consume the remaining 59 rounds.
