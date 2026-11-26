# PFSP · Zcode

This is the solver improvement implementation selected for the report. Selected version: `v038_kick`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/pfsp/improve/glm/build.sh
python3 tools/run_instance.py --solver pfsp/improve/glm --split train --instance VFR100_20_1 --seed 1111 --seconds 5 --out /tmp/codeevo-example-pfsp-improve-glm
```

[Problem, input files and build dependencies](../../../../problems/pfsp/README.md)
