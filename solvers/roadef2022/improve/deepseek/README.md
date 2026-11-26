# ROADEF 2022 · DeepSeek Harness

This is the solver improvement implementation selected for the report. Selected version: `v062`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/roadef2022/improve/deepseek/build.sh
python3 tools/run_instance.py --solver roadef2022/improve/deepseek --split train --instance PO --seed 1111 --seconds 180 --out /tmp/codeevo-example-roadef2022-improve-deepseek
```

[Problem, input files and build dependencies](../../../../problems/roadef2022/README.md)
