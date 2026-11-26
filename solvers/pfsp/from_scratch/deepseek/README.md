# PFSP · DeepSeek Harness

This is the from scratch implementation selected for the report. Selected version: `v047`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/pfsp/from_scratch/deepseek/build.sh
python3 tools/run_instance.py --solver pfsp/from_scratch/deepseek --split train --instance VFR100_20_1 --seed 1111 --seconds 5 --out /tmp/codeevo-example-pfsp-from_scratch-deepseek
```

[Problem, input files and build dependencies](../../../../problems/pfsp/README.md)
