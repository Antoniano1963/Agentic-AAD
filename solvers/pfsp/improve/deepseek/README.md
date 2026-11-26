# PFSP · DeepSeek Harness

This is the solver improvement implementation selected for the report. Selected version: `v029_double_perturb`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/pfsp/improve/deepseek/build.sh
python3 tools/run_instance.py --solver pfsp/improve/deepseek --split train --instance VFR100_20_1 --seed 1111 --seconds 5 --out /tmp/codeevo-example-pfsp-improve-deepseek
```

[Problem, input files and build dependencies](../../../../problems/pfsp/README.md)
