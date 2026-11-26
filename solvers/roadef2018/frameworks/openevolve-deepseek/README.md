# ROADEF 2018 · OpenEvolve

This is the workflow comparison implementation selected for the report.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/roadef2018/frameworks/openevolve-deepseek/build.sh
python3 tools/run_instance.py --solver roadef2018/frameworks/openevolve-deepseek --split train --instance B1 --seed 1111 --seconds 15 --out /tmp/codeevo-example-roadef2018-frameworks-openevolve-deepseek
```

[Problem, input files and build dependencies](../../../../problems/roadef2018/README.md)
