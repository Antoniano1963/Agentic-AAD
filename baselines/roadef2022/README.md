# ROADEF 2022 · truck loading · S46–HiGHS V6

This is the base algorithm supplied in the solver-improvement setting. It requires GCC/G++ with OpenMP and a Python environment containing NumPy and highspy. The build creates `s46_open`. This is the study’s V6 port of the S46 matheuristic to HiGHS, with its Python bridge and output-completeness changes. The official checker uses Java.

## Build

Run the build command from this directory on Linux.

```bash
bash build.sh
```

## Try one instance

From the repository root, the optional runner prepares the input and calls the problem checker. Use a fresh output directory.

```bash
python3 tools/run_instance.py --baseline roadef2022 --split train --instance PO --seconds 35 --seed 1111 --out runs/base-roadef2022
```

[Problem and data](../../problems/roadef2022/) · [Agent improvements](../../solvers/roadef2022/improve/)
