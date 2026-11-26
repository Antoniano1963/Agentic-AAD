# PFSP · Codex

This is the solver improvement implementation selected for the report. Selected version: `v098`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/pfsp/improve/gpt/build.sh
python3 tools/run_instance.py --solver pfsp/improve/gpt --split train --instance VFR100_20_1 --seed 1111 --seconds 5 --out /tmp/codeevo-example-pfsp-improve-gpt
```

[Problem, input files and build dependencies](../../../../problems/pfsp/README.md)
