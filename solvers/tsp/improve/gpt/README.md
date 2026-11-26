# TSP · Codex

This is the solver improvement implementation selected for the report. Selected version: `v026`.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/tsp/improve/gpt/build.sh
python3 tools/run_instance.py --solver tsp/improve/gpt --split train --instance fqm5087 --seed 1111 --seconds 15 --out /tmp/codeevo-example-tsp-improve-gpt
```

[Problem, input files and build dependencies](../../../../problems/tsp/README.md)
