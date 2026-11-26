# GLM IG Evolution Experiment Protocol (Workspace: `/data/CodexEvo/IGEvo/GLM`)

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

This document is the workspace-specific instantiation of `GLM_IG_EAX_EXPERIMENT_PROTOCOL.md`. All rules match the master protocol, with paths adapted to this workspace. All write operations are restricted to this directory.

## Task

On top of the frozen kernel `pfsp_math.py` (pure Python PFSP objective, recurrence, insertion, and basic neighborhood operators), improve Iterated Greedy (IG) for PFSP in pure Python. C/C++, JIT, and Numba are prohibited. Numerical library thread counts (e.g., numpy) are fixed to 1.

## Data Isolation

- Training set: VRF Large, `n∈{100,200}`, `m∈{20,40,60}`, 10 instances per cell, 60 instances in total, already copied into `training/` with `manifest.json` (containing UB, for evaluator use only).
- Strict test set: 180 VRF Large instances with `n=300–800`. During evolution, reading, listing, collecting statistics on, running, or indirectly utilizing them is prohibited; only the final selected version may be tested.
- The candidate solver function receives only `(matrix, seed, time_limit)`; instance names and UB exist only inside the evaluator.

## Training Evaluation

- Per-solve budget `T = n*m*0.03` seconds; candidates stop internally at approximately `0.99T`; outer `1.5T+10s` serves solely as a watchdog.
- 10 paired seeds per instance (first 4 bytes of `sha256("instance_name:rep_index")`, consistent across candidates).
- One full evaluation = `60×10 = 600` solves; at most 400 single-threaded processes.
- Metric: mean gap over 10 runs per instance → equal-weight average over 60 instances. `gap = (makespan-UB)/UB×100`.
- Per-solve verification: full permutation, independent makespan recomputation, objective consistency, and complete seed/instance coverage.
- Failures, timeouts, and invalid results must be retained and explicitly reported, never silently excluded or imputed.
- Candidate budget: 100 full training evaluations in total; `v000_reference.py` (original IG control) counts as round 1.

## Versioning and Process Discipline

- Propose only one candidate with a clear hypothesis per round; record in `DECISIONS.md`: referenced feedback, observations, hypothesis, modifications, and support/refutation criteria.
- Regenerate each candidate's complete standalone source code from the frozen kernel and save it in `history/vNNN_<name>.py`; do not copy, import, or load source code or solutions from prior candidates; only recorded summary feedback may be used.
- Candidates must first pass `check_synthetic.py` (3 instances × 2 short budgets, non-ranking), then launch one and only one full evaluation (`evaluate.py`), and only design the next version after the audit passes.
- Do not pre-design or queue subsequent versions in batch.
- Reading the training-side summary feedback and decision logs of `V1_260909` as prior knowledge is permitted; reading its candidate source code, any test instances, or any test reports is prohibited.

## Final Selection and Testing

After the training budget is exhausted, select the version with the lowest training mean gap that passed audit (`v000` does not participate), and test it on the 180 `n=300–800` instances: 10 runs per instance, `0.03nm` seconds, ≤400 single-threaded processes, with the same verification. Test results must never be used retrospectively to tune parameters or change versions.
