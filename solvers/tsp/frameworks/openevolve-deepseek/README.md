# TSP · OpenEvolve

This is the workflow comparison implementation selected for the report. Selected version: `round038`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/tsp/frameworks/openevolve-deepseek/build.sh
python3 tools/run_instance.py --solver tsp/frameworks/openevolve-deepseek --split train --instance fqm5087 --seed 1111 --seconds 15 --out /tmp/codeevo-example-tsp-frameworks-openevolve-deepseek
```

[Problem, input files and build dependencies](../../../../problems/tsp/README.md)
