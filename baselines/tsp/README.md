# Traveling salesman · GA-EAX Large

This is the base algorithm supplied in the solver-improvement setting. It uses GCC/G++ and Python 3. The build creates `jikken`. The supplied configuration uses a population of 300, 30 offspring, and the archived test restart cap of 10.

## Build

Run the build command from this directory on Linux.

```bash
bash build.sh
```

## Try one instance

From the repository root, the optional runner prepares the input and calls the problem checker. Use a fresh output directory.

```bash
python3 tools/run_instance.py --baseline tsp --split train --instance fqm5087 --seconds 6 --seed 1111 --out runs/base-tsp
```

[Problem and data](../../problems/tsp/) · [Agent improvements](../../solvers/tsp/improve/)
