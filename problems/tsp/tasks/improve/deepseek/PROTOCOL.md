# DeepSeek EAX Evolution Experiment Working Protocol (V1)

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

This document is the execution protocol for the DeepSeek workspace. It is consistent with the EAX section of the project-root `GLM_IG_EAX_EXPERIMENT_PROTOCOL.md`, with every parameter verified item by item on the server.

## Fixed Rules

- Problem: Euclidean TSP. Frozen base: GA-EAX-total-budget Large variant (shared total-budget implementation), with the source snapshot stored in `frozen_base/` (source hashes locked in `FROZEN_BASE_SHA256`).
- Each candidate = `history/vNNN_name/`: `source/` (complete, independently compilable C/C++ source tree) + `run_config.json` + `EDITS.md` (explicit edit log relative to the frozen base) + `build.sh` + `build.log`. Regenerate from `frozen_base/`; copying or importing from any previous candidate is strictly prohibited.
- BKS is used solely by the evaluator to compute gap and is strictly prohibited as input for termination, instance classification, or search decisions in any candidate.
- Training set: 33 instances (`5000≤n≤20000`, 6 Easy + 27 Medium, excluding `usa13509`), listed in `manifest.json` (including BKS and sha256).
- Strict isolation: during the evolution phase, do not read, run, or collect statistics on the 21 test instances with `n>20000` or `usa13509`, and do not read the training feedback, decision logs, candidate source code, or result files of GPT (`/data/CodexEvo/EAXEvo/history/`), GLM (`/data/CodexEvo/EAXEvo/GLM/`), or KIMI (`/data/CodexEvo/EAXEvo/KIMI/`). Use only: this protocol, the frozen base, the training instances, and this workspace's own evaluation feedback. The only reference anchor taken is the public protocol figure: GPT training best `0.003480829%`.
- **Objective**: not merely to beat GPT, but to design the best possible EAX algorithm.

## Evaluation Configuration

| Item | Setting |
|---|---|
| Total budget per run | `n≤10000` → 900s; `10000<n≤20000` → 1200s (timed from before loading/preprocessing, including initialization, all searches, all restarts, and output) |
| Internal restarts | At most 5 restarts after the initial search = at most 6 searches, sharing the same total budget; the frozen base itself has no upper limit, so **every candidate applies the same compliance patch `n < 6` in `main.cpp`** (infrastructure-level compliance, recorded permanently as the first item in `EDITS.md`) |
| Seeds | 1111–1120, identical across candidates |
| One complete evaluation | 33×10 = 330 runs, ≤400 single-threaded processes, each run in a fresh process and output directory |
| Validation | Exit code, `1≤searches≤6`, `elapsed≤budget+60s`, `BestSol` is a valid permutation of `1..N`, independent Euclidean `nint` recomputation == `best_length` |
| Ranking | Mean of 10 best gaps per instance → unweighted mean across 33 instances; `eligible` only if 330/330 succeed |
| Budget | 50 complete evaluations in total; `v001` same-budget baseline counts as evaluation #1 |

## File Conventions

- `evaluations/NNN_<ver>/`: `runs.jsonl` + `summary.json` + `runs/` (each run retains `eax_BestSol`, `eax_total.json`, `eax_convergence_total.csv`)
- `ledger.json`: round-by-round ledger; `DECISIONS.md`: round-by-round hypotheses, rationale, results, and decisions
- `evaluate.py` / `audit_eval.py` / `tools/smoke_test.py`: evaluation controller / independent auditor / synthetic smoke test

## Termination and Testing

After the 50-evaluation budget is exhausted or the candidate pool is exhausted, select the `eligible` version in `ledger.json` with the lowest mean gap as the final selection; strict testing on the 21 test instances (3600s, at most 11 searches, `python3 test/run_test.py <ver>`) requires separate user authorization and is executed only after the administrator places the test manifest and instances into `test/`; test results must never be fed back to modify any version.
