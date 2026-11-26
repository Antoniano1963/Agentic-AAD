# ROADEF 2018 · Zcode

This is the solver improvement implementation selected for the report. Selected version: `iter020 + iter016`.

The iter020 wrapper uses the source associated with iter016. Its byte-for-byte correspondence to the submitted Linux executable has not been established. The command below builds the available source.

Run from the repository root on Linux with a fresh output directory.

```sh
bash solvers/roadef2018/improve/glm/build.sh
python3 tools/run_instance.py --solver roadef2018/improve/glm --split train --instance B1 --seed 1111 --seconds 15 --out /tmp/codeevo-example-roadef2018-improve-glm
```

[Problem, input files and build dependencies](../../../../problems/roadef2018/README.md)
