# ROADEF 2020 · Zcode

This is the solver improvement implementation selected for the report. Selected version: `cand_07_v8`.

The complete [source tree](src/) builds directly on Linux with GCC/G++, OpenMP and the C++17 standard library. The [base ALNS](../../../../baselines/roadef2020/) is the starting point for this variant.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/roadef2020/improve/glm/build.sh
python3 tools/run_instance.py --solver roadef2020/improve/glm --split train --instance C_02 --seed 1111 --seconds 180 --out /tmp/codeevo-example-roadef2020-improve-glm
```

[Problem, input files and build dependencies](../../../../problems/roadef2020/README.md)

The compiled executable is `solver`. On an unpacked benchmark input, call `./solver -p INSTANCE.json -o solution.txt -t SECONDS -s SEED` from this directory.
