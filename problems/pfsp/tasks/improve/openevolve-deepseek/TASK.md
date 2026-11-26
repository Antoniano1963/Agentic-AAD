# OpenEvolve + DeepSeek: Improving the Original IG (PFSP)

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Starting from the historical original Python IG, this experiment allows modifying the entire algorithm—including initialization, destruction, reconstruction, local search, and acceptance criteria—while preserving the single-file Python CLI interface. Both the framework and candidates run under the single account `openevolve_pfsp_run02`.

## Fixed Protocol
- Training data: `pfsp/training/`; manifest `pfsp/training_manifest.json` (containing input hashes, fixed seeds, and reference upper bounds). 60 instances with `n=100/200` and `m=20/40/60`, 10 repeats per instance, totaling 600 solves per full evaluation.
- Per solve: 1 logical CPU, `0.03*n*m` seconds; 400 disjoint CPU slots, with candidate evaluations run serially. Retains the historical `watchdog=1.5*T+10` seconds; all nominal timeouts are recorded concurrently, and watchdog grace time must not be treated as extra search budget.
- Budget: 50 candidate evaluation attempts in total, including the initial algorithm and failed candidates; model generation failures are logged separately and not fabricated as completed evaluations; no off-ledger search is permitted. If the framework exhausts its generation iterations early, resume from a checkpoint to complete the remaining evaluations without re-evaluating the initial algorithm. Stop and report upon persistent API failures.
- Entry point: `pfsp/evaluator.py -> pfsp/tools/evaluate_train.py`. Freezes each candidate and independently verifies permutations and makespan; only candidates with all 600 solves valid participate in quality ranking. `mean gap = (makespan - BKR) / BKR * 100%`, equally weighted across instances; `best gap` is the equal-weight average of each instance's best gap across 10 repeats. BKR is a fixed reference upper bound, so negative gaps are allowed.
- `combined_score = -mean_gap`; ineligible candidates receive `-1e12`. Select the best candidate on the training set; do not access or evaluate the final test set.
- Operate exclusively within this working directory; do not access other agents or test data, modify the evaluation protocol or training data, or access the network or API keys inside candidate code. An external UID observer records total account resource usage without interfering with the experiment.

## Run Records
OpenEvolve 0.4.0, source commit recorded in `UPSTREAM_COMMIT`; 5 islands, `temperature=1`, `max_tokens=65536`, single-evaluation concurrency. Actual DeepSeek request/response model names are recorded in `pfsp/api_preflight.json` and `config.yaml`; interface aliases are not treated as proven historical model versions.
Candidates: `pfsp/candidates/`; full feedback: `pfsp/evaluations/`; attempt ledger: `pfsp/attempts.jsonl`; framework logs/prompts/checkpoints: `pfsp/logs/` and `pfsp/outputs/`.
Initial code provenance is recorded in `pfsp/source_provenance.json`, with only CLI wrapper adjustments.
UID monitoring provides a sampled lower bound on cumulative CPU usage; the evaluator additionally records `wait4` CPU time, RSS, and wall-clock time. Both the framework and candidates are accounted for under the same account. Model connectivity checks and tiny synthetic-instance interface checks are logged separately and do not count as formal training evaluations.
