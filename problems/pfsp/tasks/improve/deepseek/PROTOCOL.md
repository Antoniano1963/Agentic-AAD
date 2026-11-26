# DeepSeek IG Evolution Experiment Working Protocol (V1)

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

This document is the execution protocol for the DeepSeek workspace. It is consistent with the IG section of `GLM_IG_EAX_EXPERIMENT_PROTOCOL.md` in the project root, with all parameters verified item by item on the server.

## Fixed Rules

- Problem: PFSP (Permutation Flow Shop Scheduling Problem), evolving Iterated Greedy in Python.
- Frozen kernel `pfsp_math.py` (sha256 locked in `FROZEN_KERNEL_SHA256`), which only provides objective evaluation, recurrence, insertion, and basic neighborhood operators, without any search strategy. **Modification is prohibited.**
- Training set: 60 VRF Large instances (`n∈{100,200} × m∈{20,40,60} × 10`), listed in `training/manifest.json` (including UB and sha256; **UB is strictly for the evaluator to compute gap and must not be used by candidate code**).
- Strict isolation: During the evolution phase, do not read, run, or collect statistics on the 180 test instances with `n=300–800`; this workspace contains no test instances (`testing/` is empty and will be populated by the administrator only after authorization).
- Independent evolution: Do not read training feedback, decision records, candidate source code, or result files from GPT (`/data/IGEvo/...`), GLM (`/data/CodexEvo/IGEvo/GLM/...`), or KIMI (`/data/CodexEvo/IGEvo/KIMI/...`). Only use this protocol, the frozen kernel, the training instances, and the evaluation feedback accumulated within this workspace.
- Candidate contract: `solve(processing, seed, time_limit) -> list`, pure Python, no C/C++/JIT/Numba/multiprocessing; the candidate only receives the processing time matrix, a deterministic seed, and the time budget, and must not read instance names, UB, any external files, environment variables, or the network.
- Propose only one candidate with a clear hypothesis per round, regenerating complete standalone source code from the frozen base (`v000_reference.py`); design the next round only after full evaluation and auditing are complete.

## Evaluation Configuration

| Item | Setting |
|---|---|
| Soft budget | `T = n*m*0.03` seconds (60s–360s); candidate stops internally at approximately `0.99T` |
| Watchdog | `1.5T + 10` seconds (fault-handling fallback only; timeouts are recorded as failures) |
| Repeats | 10 deterministic paired seeds per instance: `sha256("DeepSeek|{instance}|{rep}") mod 2^31`, consistent across candidates |
| One full evaluation | 600 solves, at most 400 single-threaded processes, numerical library threads = 1, a fresh process per solve |
| Verification | Permutation validity + independent makespan recomputation by the evaluator (candidate returns only the permutation) + `gap = 100*(L-UB)/UB` |
| Ranking | Mean gap over 10 runs per instance → equal-weight average across 60 instances; eligible only if 600/600 succeed |
| Budget | 100 full evaluations in total; the `v000` original IG baseline counts as round 1 |

## File Conventions

- `history/vNNN_name.py`: complete candidate source code
- `evaluations/NNN_*/`: `runs.jsonl` + `summary.json` + `audit.json`
- `ledger.json`: round-by-round ledger; `DECISIONS.md`: per-round hypothesis, rationale, results, and decisions
- `evaluate.py` / `audit_eval.py` / `check_synthetic.py`: evaluation controller / independent audit / candidate contract self-check

## Termination and Testing

After the 100-evaluation budget is exhausted or the candidate pool is depleted and the run concludes, select the version in `ledger.json` that is `eligible` and has the lowest mean gap as the final selection. Strict testing on the 180 instances (`T=n*m*0.06` seconds, 10 paired-seed repeats) may only be executed after separate user authorization (`python3 test_strict.py history/vNNN_name.py`), and test results must never be fed back to modify versions.
