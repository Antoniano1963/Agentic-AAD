# DeepSeek IG Evolution Experiment Task Specification

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Hello, DeepSeek. You will conduct a **PFSP Iterated Greedy evolution experiment** on the `cpu3` server. The environment is completely set up; you do not need to install anything, and all commands have been verified to work. Please read this document word for word before getting started.

## 0. One-Sentence Task

Within a **100-round evaluation budget**, evolve a Python Iterated Greedy algorithm for PFSP to make the equal-weight mean gap across the 60 training instances **as low as possible**.

Reference anchor (for calibration only, not your target ceiling): GPT's `v098` training score is **1.296465%** (full 100-round budget). Your goal is to design the best IG algorithm possible.

## 1. Environment and Paths

- Your workspace (**you may only modify files inside this directory**): `/data/CodexEvo/IGEvo/DeepSeek/`
- System `python3` is directly available (standard library only; **do not use pip to install any packages**)
- Execute all commands inside the workspace directory: first run `cd /data/CodexEvo/IGEvo/DeepSeek`

## 2. What Is in the Workspace

| File / Directory | Purpose | Modifiable |
|---|---|---|
| `PROTOCOL.md` | Full protocol (companion to this document; also required reading) | No |
| `pfsp_math.py` | Frozen kernel: PFSP objective, recurrence, insertion, and basic neighborhood operators | **Prohibited** |
| `FROZEN_KERNEL_SHA256` | Kernel hash lock, verified during evaluation | No |
| `training/` | 60 training instances + `manifest.json` (contains UB; **for evaluator use only**) | No |
| `history/v000_reference.py` | Original IG baseline (common ancestor of all candidates) | No |
| `evaluate.py` | One full training evaluation (600 solves) | No |
| `audit_eval.py` | Independently audit a given evaluation round | No |
| `check_synthetic.py` | Candidate contract self-check (must be run before evaluation) | No |
| `ledger.json` | Round-by-round ledger (automatically appended by evaluation) | Do not edit manually |
| `DECISIONS.md` | Your decision log (required for every round) | **Yes** |
| `history/` | Where you place your candidates (`vNNN_name.py`) | **Yes** (add new files only) |
| `testing/`, `strict_test/` | Strict test related (currently empty; see Section 7) | No |

## 3. Hard Rules (Violating Any Rule = Round Invalidated)

1. **You may only modify files inside `/data/CodexEvo/IGEvo/DeepSeek/`.** Do not read any content under the GPT (`/data/IGEvo/...`), GLM (`/data/CodexEvo/IGEvo/GLM/`), or KIMI (`/data/CodexEvo/IGEvo/KIMI/`) directories.
2. **Strictly prohibited** from reading, running, or collecting statistics on the `n=300–800` test instances or any information about them; `testing/` must remain empty.
3. Candidate contract: `solve(processing, seed, time_limit) -> list`, returning a permutation of `0..n-1`.
   - `processing`: `n×m` processing time matrix (list of lists); `seed`: `int`; `time_limit`: seconds (`float`).
   - **Pure Python**; C/C++/JIT/Numba/multiprocessing/multithreading/file I/O/network/environment variables are prohibited.
   - Candidates may only use the three input arguments and the general utilities in `pfsp_math.py`; **branching on instance names, identity features beyond size class, UB, or any external information is prohibited**.
   - Must stop on its own and return a solution before approximately `0.99 * time_limit`.
4. Propose **only one candidate with a clear hypothesis per round**: write the hypothesis first, then evaluate, wait for results, and then design the next round. Pre-designing and queuing multiple versions in advance is prohibited.
5. Each candidate must be **rewritten from scratch** as a complete standalone file from `history/v000_reference.py` (the frozen base); copying, importing, or loading the source code of any prior candidate is prohibited. You may draw on experimental conclusions recorded in `DECISIONS.md`.
6. Each evaluation round may be **launched only once**; preserve and record failures, and never secretly rerun or delete failure records.

## 4. Standard Per-Round Workflow (Follow Verbatim)

Using Round 2 and candidate name `v001_myidea.py` as an example (**Round 1 is the baseline `v000`, already completed by the administrator and recorded in the ledger; you start from Round 2**):

```bash
cd /data/CodexEvo/IGEvo/DeepSeek

# Step 1: First append this round's plan to the end of DECISIONS.md (see Section 5 for format)
#         Using an editor or: cat >> DECISIONS.md << 'EOF' ... EOF

# Step 2: Write the candidate file history/v001_myidea.py (complete standalone source code)

# Step 3: Contract self-check (takes seconds; must pass first)
python3 check_synthetic.py history/v001_myidea.py

# Step 4: Launch full evaluation (approx. 10–15 minutes; wait during execution and do not run other heavy jobs)
python3 evaluate.py 2 v001_myidea.py

# Step 5: Audit (takes seconds)
python3 audit_eval.py 2

# Step 6: Record results and decision in DECISIONS.md (numbers are in evaluations/002_v001_myidea/summary.json)
```

Note: The second argument to `evaluate.py` is the **filename** inside `history/` (without a directory path). Evaluation number `N` corresponds one-to-one with filename `vNNN` and increments consecutively.

## 5. `DECISIONS.md` Per-Round Record Format

```markdown
## Round N — vNNN_name

- **Hypothesis**: (Which completed feedback was referenced / what was observed / why it may work / specific changes / what outcome would support or refute it)
- **Result**: mean gap = X.XXXXXX% (600/600 or failure details)
- **Decision**: Adopt as new comparison baseline / Reject (reason) / Plan for next round
```

## 6. SSH Disconnection / Interruption Recovery

- Evaluations run in the foreground on the server, so SSH disconnection will interrupt them. After reconnecting, check:
  ```bash
  ps aux | grep evaluate.py | grep -v grep     # Output present = still running, wait for it
  ls /data/CodexEvo/IGEvo/DeepSeek/evaluations/ # Check whether the last round finished writing summary.json
  tail -2 /data/CodexEvo/IGEvo/DeepSeek/ledger.json  # Last round in the ledger
  ```
- If a round was indeed interrupted (no `summary.json`): remove that round's directory with `rm -rf evaluations/NNN_*` and rerun `evaluate.py` with the **same round number**. This does not count as a secret rerun—an interrupted round produced no result.
- Running long tasks with `nohup ... &` + a log file is recommended, for example:
  ```bash
  cd /data/CodexEvo/IGEvo/DeepSeek
  nohup python3 evaluate.py 2 v001_myidea.py > evaluations/eval002_console.log 2>&1 &
  ```
  Then check progress every few minutes with `tail evaluations/eval002_console.log` (which prints lines like `progress 60/600`).

## 7. Termination and Strict Testing

- Stop and conclude when the **100-round budget is exhausted**, or when you judge the candidate pool to be depleted (multiple consecutive rounds without substantive progress and all directions closed by evidence).
- Upon concluding: select the version from `ledger.json` that is **eligible and has the lowest mean gap** as the final selection, write a final summary into `DECISIONS.md`, and then **stop and report**.
- Strict testing (180 instances with `n=300–800`, `T=n×m×0.06` seconds, 10 seeds) **requires user authorization first**; after the administrator places the test instances into `testing/`, run (note: the argument is the **filename inside `history/` without a path prefix**, as the script prepends it automatically):
  ```bash
  python3 test_strict.py vNNN_final.py
  ```
  Test results are a final measurement and must **never be fed back to modify versions**.

## 8. General Evolution Guidelines (Identical to Those Provided to All Prior Experimenters)

1. First read the protocol, the frozen base implementation, the training instance manifest, and the summary results and decision logs of completed training evaluations.
2. Propose only one clear hypothesis at a time; state what feedback was referenced, why it may work, and what outcome will support or refute it.
3. Launch a full evaluation only after passing the synthetic correctness check; evaluate each round only once.
4. Use a round's result to design the next version only after it passes audit.
5. Do not pre-design multiple proposals in batch; wait for full feedback on the current candidate before deciding the next step.
6. Stop training once the budget is exhausted and select the version with the lowest training mean gap that passed audit.

Best wishes for a smooth experiment. Proceed steadily and document the "why" for every round clearly.
