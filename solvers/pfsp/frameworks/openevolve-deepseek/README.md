# PFSP · OpenEvolve

This is the workflow comparison implementation selected for the report. Selected version: `round046`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/pfsp/frameworks/openevolve-deepseek/build.sh
python3 tools/run_instance.py --solver pfsp/frameworks/openevolve-deepseek --split train --instance VFR100_20_1 --seed 1111 --seconds 5 --out /tmp/codeevo-example-pfsp-frameworks-openevolve-deepseek
```

[Problem, input files and build dependencies](../../../../problems/pfsp/README.md)
