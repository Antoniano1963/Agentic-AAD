# Permutation flow shop · Iterated greedy

This is the base algorithm supplied in the solver-improvement setting. It uses Python 3 with NumPy. The build checks the Python sources. The original IG search and permutation checker are included.

## Build

Run the build command from this directory on Linux.

```bash
bash build.sh
```

## Try one instance

From the repository root, the optional runner prepares the input and calls the problem checker. Use a fresh output directory.

```bash
python3 tools/run_instance.py --baseline pfsp --split train --instance VFR100_20_1 --seconds 5 --seed 1111 --out runs/base-pfsp
```

[Problem and data](../../problems/pfsp/) · [Agent improvements](../../solvers/pfsp/improve/)
