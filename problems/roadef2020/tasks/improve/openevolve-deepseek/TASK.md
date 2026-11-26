# OpenEvolve + DeepSeek: ROADEF2020 Baseline Algorithm Improvement

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Working directory: `/data/CodeEvo/ROADEF2020/agent_runs/improve_alns/OpenEvolve_DeepSeek_run01`; dedicated account: `openevolve_r20_run01`.
Starting point: original J49 ALNS, without using Gurobi/CPLEX or improvements from other agents.
Training set: C01–C15, with data in `initial_materials/data/train/`; no test set is provided.
Each round consists of 15 instances × 3 repetitions, 45 runs in total; each batch launches 15 instances simultaneously, the three batches complete sequentially, and rounds do not overlap.
Single-run total wall-clock limit is 900 seconds, with an internal budget of 850 seconds; at most 4 logical CPUs, and process-tree memory limit of 16 GB (decimal).
50 full evaluation rounds, with the first round evaluating the original algorithm counted in the budget; compilation failures are recorded separately and do not count toward full rounds; runtime failures and invalid solutions are not deleted.
Uses the original OpenEvolve version and DeepSeek API configuration, output limit 65536 tokens, with single-candidate evaluations executed sequentially. The entire algorithm source code evolves in literal FILES format, allowing substantive algorithmic modifications.
Validity rate takes priority; once all 45 runs are valid and compliant, candidates are compared by the full-set mean official risk objective value. All per-instance mean/best values and failure reasons are preserved.
The fixed evaluator, official checker, materials, and training data may not be modified; the model may only generate candidate source code and is not allowed to perform extra screening, read other experiments, or automatically evaluate on an independent test set.
External monitoring tracks CPU and memory across the entire account; the evaluator records per-run CPU, RSS, wall-clock time, code hash, and checker results. There is no hard cgroup memory limit; the monitoring itself does not intervene in the experiment. The 900-second evaluation deadline is enforced by the fixed evaluator.
The complete model input is saved in `experiment/prompt.txt`; the full original text of the official problem statement is included directly in the input rather than providing only a file path.
