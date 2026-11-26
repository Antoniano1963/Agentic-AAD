# ROADEF 2018 · Kimi-code

This is the solver improvement implementation selected for the report. Selected version: `v015`.

The v015 source includes the seven-search default for unseen instances. The portable build uses the system compiler. The original experiment also used PGO, LTO and mimalloc.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/roadef2018/improve/kimi/build.sh
python3 tools/run_instance.py --solver roadef2018/improve/kimi --split train --instance B1 --seed 1111 --seconds 15 --out /tmp/codeevo-example-roadef2018-improve-kimi
```

[Problem, input files and build dependencies](../../../../problems/roadef2018/README.md)
