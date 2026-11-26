# PFSP · Kimi-code

This is the solver improvement implementation selected for the report. Selected version: `v057_k50`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/pfsp/improve/kimi/build.sh
python3 tools/run_instance.py --solver pfsp/improve/kimi --split train --instance VFR100_20_1 --seed 1111 --seconds 5 --out /tmp/codeevo-example-pfsp-improve-kimi
```

[Problem, input files and build dependencies](../../../../problems/pfsp/README.md)
