# OpenEvolve + DeepSeek: ROADEF2018 Original Algorithm Improvement

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Working directory: `/data/CodexEvo/ROADEF2018/agent_runs/improve_champion/OpenEvolve_DeepSeek_run01`; isolated account: `openevolve_r18_run01`.
Starting point: the original champion algorithm, without using improved code from KIMI/GLM/DeepSeek.
Training set: B1–B15, with data located in `initial_materials/data/train/`; no test set is provided.
Each round runs 15 instances × 3 repeats, totaling 45 runs; each batch launches all 15 instances simultaneously, the three batches complete sequentially, and rounds do not overlap.
Per-run total wall-clock limit is 900 seconds, with an internal budget of 870 seconds; at most 8 logical CPUs and 48 GB (decimal) process-tree memory.
50 full evaluation rounds in total, with the original algorithm counted as the first round; compilation failures are logged separately and do not count toward full rounds; execution failures and invalid solutions are not deleted.
Uses the original OpenEvolve version and DeepSeek API configuration, with an output limit of 65536 tokens, evaluating candidates sequentially one at a time. The entire algorithm source code evolves as literal `FILES`, allowing substantive algorithmic modifications.
Validity rate takes priority; once all 45 runs are valid and compliant, candidates are compared by full-set mean waste%. All per-instance mean/best metrics and failure reasons are retained.
The fixed evaluator, official checker, reference materials, and training data must not be modified; the model may only generate candidate source code, and is not permitted to perform extra screening, read other experiments, or automatically evaluate on the independent test set.
CPU and memory are tracked externally across the entire account; the evaluator records per-run CPU, RSS, wall-clock time, code hash, and checker results. There is no cgroup hard memory limit, and monitoring itself does not intervene in the experiment. The 900-second evaluation cutoff is enforced by the fixed evaluator.
The complete model input is saved in `experiment/prompt.txt`, with the full official problem text included directly in the input rather than only providing a file path.
