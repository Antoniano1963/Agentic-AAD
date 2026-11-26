# DeepSeek EAX Evolution Experiment Task Specification

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Hello, DeepSeek. You will conduct a **TSP EAX evolution experiment** on the `cpu3` server. The environment is completely set up and all commands have been verified to work. Please read this document and `PROTOCOL.md` word for word before starting.

## 0. One-Sentence Task

Within a **50-round evaluation budget**, improve the algorithm design on top of the frozen GA-EAX (Large variant, shared total budget) to make the unweighted mean gap across the 33 training instances **as low as possible**.

Reference anchor (for calibration only, not an upper bound on your goal): GPT's `v026` training score **0.003480829%** (full 50-round budget). Your goal is to design the best possible EAX algorithm.

## 1. Environment and Paths

- Your workspace (**you are only allowed to modify files inside this directory**): `/data/CodexEvo/EAXEvo/DeepSeek/`
- Compiler: system `g++` is directly available (`-O3`); system `python3` is directly available (standard library only; **do not `pip install` any packages**)
- Execute all commands inside the workspace directory: first run `cd /data/CodexEvo/EAXEvo/DeepSeek`

## 2. Workspace Contents

| File/Directory | Purpose | Modifiable? |
|---|---|---|
| `PROTOCOL.md` | Full protocol (required reading) | No |
| `frozen_base/` | Frozen base source code (GA-EAX Large total-budget variant) | **Prohibited** |
| `FROZEN_BASE_SHA256` | Frozen base hash lock | No |
| `manifest.json` + `training/` | 33 training instances (with BKS/sha256; **only the evaluator may use BKS**) | No |
| `history/v001_baseline/` | Baseline candidate (round 1 already evaluated and recorded) | No (read-only reference) |
| `evaluate.py` | One complete training evaluation round (330 runs) | No |
| `audit_eval.py` | Independently audits a given evaluation round | No |
| `tools/smoke_test.py` | Synthetic-instance smoke test (must pass before evaluation) | No |
| `ledger.json` | Round-by-round ledger (automatically appended by evaluation) | Do not edit manually |
| `DECISIONS.md` | Your decision log (required every round) | **Yes** |
| `history/` | Directory for your candidates (`vNNN_name/` directories) | **Yes** (append only) |
| `test/` | Strict test runner (instances placed only after authorization; see Section 8) | No |

## 3. Hard Rules (Violating Any Rule = Round Invalidated)

1. **You may only modify files inside `/data/CodexEvo/EAXEvo/DeepSeek/`.** Do not read anything under the GPT (`/data/CodexEvo/EAXEvo/history/`), GLM (`.../GLM/`), or KIMI (`.../KIMI/`) directories.
2. **Strictly prohibited** from reading, running, or collecting statistics on any instance with `n>20000`, `usa13509`, or any information about them.
3. **BKS is only for the evaluator to compute gap**: candidate code is prohibited from using BKS as a stopping criterion, instance classifier, or search decision input.
4. Each candidate must **copy the full source tree freshly from `frozen_base/`** before applying edits; copying, importing, or loading source code or binaries from any previous candidate is prohibited. You may draw upon experimental conclusions recorded in `DECISIONS.md`.
5. **Every candidate must include the same compliance patch** (infrastructure-level, not an algorithmic change) in the total-budget loop of `source/main.cpp`:
   ```
   -  for(int n=0; eax_curve.total_mode || n<maxNumOfTrial; ++n) {
   +  for(int n=0; eax_curve.total_mode ? n < 6 : n < maxNumOfTrial; ++n) {
   ```
   Always record this permanently as the first item in `EDITS.md`, followed by your algorithmic edits item by item.
6. Propose **only one candidate with an explicit hypothesis per round**: write the hypothesis first, run the evaluation, wait for results, and then design the next round. Pre-designing and queueing multiple versions in batches is prohibited.
7. Launch each round's evaluation **only once**; preserve and record failures, and never secretly rerun or delete failed records.

## 4. Candidate Directory Structure (Follow `v001_baseline`)

```
history/v002_myidea/
├── source/          # Complete C++ source tree (copied from frozen_base + compliance patch + your edits)
├── run_config.json  # Run configuration (see below)
├── EDITS.md         # Explicit edit log relative to frozen_base (item 1 is always the compliance patch)
├── build.sh         # Build script (copy from v001_baseline)
└── build.log        # Compilation output of bash build.sh
```

Two formats for `run_config.json` (both accepted by the evaluator):
```json
{"population_rule": "300_if_n>10000_else_100", "offspring": 30, "seeds": "1111-1120", "max_searches": 6}
```
Or the tiered format (split at `n=10000`, matching the budget tiers):
```json
{"population_large": 100, "population_small": 100, "offspring": 30, "seeds": "1111-1120", "max_searches": 6}
```
`population_*` is the population size and `offspring` is the number of offspring per crossover. These are merely command-line arguments passed to the binary; **the real algorithmic changes live in `source/`**.

## 5. Standard Per-Round Workflow (Follow Verbatim)

Using Round 2 and candidate name `v002_myidea` as an example (**Round 1 baseline `v001_baseline` has already been completed and logged by the administrator; you start at Round 2**):

```bash
cd /data/CodexEvo/EAXEvo/DeepSeek

# Step 1: Write this round's hypothesis at the end of DECISIONS.md (see Section 6 for format)

# Step 2: Build the candidate
mkdir -p history/v002_myidea/source
cp frozen_base/*.cpp frozen_base/*.h history/v002_myidea/source/
#   Apply the compliance patch (one-line sed):
sed -i 's/eax_curve.total_mode || n<maxNumOfTrial/eax_curve.total_mode ? n < 6 : n < maxNumOfTrial/' history/v002_myidea/source/main.cpp
#   Apply your algorithmic edits; write EDITS.md and run_config.json; copy build.sh
cp history/v001_baseline/build.sh history/v002_myidea/

# Step 3: Compile
cd history/v002_myidea && bash build.sh && cd ../..
#   Success indicator: history/v002_myidea/source/jikken is produced; warnings are fine, check build.log for errors

# Step 4: Smoke test (~10-20 seconds, must pass first)
python3 tools/smoke_test.py history/v002_myidea/source/jikken

# Step 5: Launch complete evaluation (~15-20 minutes)
python3 evaluate.py 2 v002_myidea

# Step 6: Audit
python3 audit_eval.py 2

# Step 7: Record results and decision in DECISIONS.md (numbers are in evaluations/002_v002_myidea/summary.json)
```

Note: The second argument to `evaluate.py` is the **directory name** inside `history/` (without the `history/` prefix). The evaluation number `N` corresponds 1-to-1 with `vNNN` and increments consecutively.

## 6. `DECISIONS.md` Per-Round Record Format

```markdown
## Round N — vNNN_name

- **Hypothesis**: (Which completed feedback was referenced / what was observed / why it may work / exact edits / what outcome would support or refute it)
- **Result**: mean gap = X.XXXXXX% (330/330 or failure details)
- **Decision**: Adopt as new comparison baseline / Reject (reason) / Next-round plan
```

## 7. Permitted Free Exploration (Does Not Consume the 50-Round Budget)

You may use your own small scripts on the **training instances** to freely run candidate binaries for probing experiments (binary usage: `EAX_TOTAL_TIME_LIMIT=<seconds> EAX_BKS=<bks_from_manifest> EAX_SEED=<seed> ./jikken 1 eax <population> <offspring> <instance_path>`, writing `eax_total.json` / `eax_BestSol` / `eax_convergence_total.csv` into the working directory). This does not count as a formal evaluation and does not consume budget, making it a useful tool to verify mechanisms. However, the **verdict** for each round must come from one complete formal evaluation.

## 8. Interruption Recovery and Strict Testing

- An SSH disconnect will interrupt a foreground evaluation. After reconnecting, check:
  ```bash
  ps aux | grep evaluate.py | grep -v grep     # Output present = still running, wait for it
  ls /data/CodexEvo/EAXEvo/DeepSeek/evaluations/   # Check if the latest round has summary.json
  tail -2 /data/CodexEvo/EAXEvo/DeepSeek/ledger.json
  ```
  For an interrupted round (no `summary.json`): run `rm -rf evaluations/NNN_*` and rerun with the **same number** (this does not count as an unauthorized rerun).
- Running long tasks in the background is recommended:
  ```bash
  nohup python3 evaluate.py 2 v002_myidea > evaluations/eval002_console.log 2>&1 &
  tail -f evaluations/eval002_console.log   # Prints progress lines such as progress 30/330
  ```
- When the **50-round budget is exhausted** or you judge that the candidate pool is exhausted → select the **`eligible` version with the lowest mean gap** from `ledger.json` as the final selection, write a final summary in `DECISIONS.md`, and **stop and report**.
- Strict testing (21 instances with `20000<n≤50000`, 3600s, ≤11 searches, seeds 1111–1120) **requires user authorization**; after the administrator places the test manifest and instances into `test/`:
  ```bash
  python3 test/run_test.py <final_version_dir_name>
  ```
  Test results are a terminal measurement and must **never be fed back to modify any version**.

## 9. General Evolution Guidelines (Identical to Those Provided to All Previous Experimenters)

1. First read the protocol, frozen base implementation, training instance manifest, summary results of completed training evaluations, and decision logs.
2. Propose only one explicit hypothesis at a time; state what feedback was referenced, why it may work, and what results would support or refute it.
3. Complete the smoke check before launching a full evaluation; evaluate each round only once.
4. Use the result to design the next version only after the audit passes.
5. Do not pre-design multiple schemes in batches; wait for full feedback on the current candidate before deciding the next step.
6. Stop training after the budget is exhausted, and select the version with the lowest training mean gap that passed auditing.

Good luck with the experiment. Proceed steadily and document the "why" of every round clearly.
