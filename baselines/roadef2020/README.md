# ROADEF 2020 · grid maintenance · Adaptive large neighborhood search (ALNS)

This is the base algorithm supplied in the solver-improvement setting. It requires a C++17 compiler with OpenMP support. The build creates `solver` from the complete local [source tree](src/). The study uses the archived original ALNS from the J49 team as its human baseline. [Upstream source](https://github.com/wolledav/ROADEF_2020/tree/fb1fc685423e2710bbaada77d8975d94333d8dc4)

## Build

Run the build command from this directory on Linux.

```bash
bash build.sh
```

## Try one instance

From the repository root, the optional runner prepares the input and calls the problem checker. Use a fresh output directory.

```bash
python3 tools/run_instance.py --baseline roadef2020 --split train --instance C_02 --seconds 120 --seed 1111 --out runs/base-roadef2020
```

[Problem and data](../../problems/roadef2020/) · [Agent improvements](../../solvers/roadef2020/improve/)

Once built, the standalone executable accepts an unpacked RTE instance directly. The following command assumes `C_02.json` is in the repository root.

```bash
baselines/roadef2020/solver -p C_02.json -o solution.txt -t 120 -s 1111
```
