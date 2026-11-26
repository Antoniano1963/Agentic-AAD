# ROADEF 2018 · glass cutting · Champion-derived tree search

This is the base algorithm supplied in the solver-improvement setting. It requires GCC/G++ and Boost Program Options (Ubuntu package `libboost-program-options-dev`). The build creates `solver`. The supporting headers and shared glass-cutting parameters are included in this directory.

## Build

Run the build command from this directory on Linux.

```bash
bash build.sh
```

## Try one instance

From the repository root, the optional runner prepares the input and calls the problem checker. Use a fresh output directory.

```bash
python3 tools/run_instance.py --baseline roadef2018 --split train --instance B1 --seconds 8 --seed 1111 --out runs/base-roadef2018
```

[Problem and data](../../problems/roadef2018/) · [Agent improvements](../../solvers/roadef2018/improve/)
