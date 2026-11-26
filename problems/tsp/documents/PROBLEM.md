# Symmetric Euclidean TSP: Problem Specification and File Format

> **Translation note:** This file is an English translation of the original Chinese problem specification provided during the experiments.

Given n two-dimensional coordinate points, construct a closed tour that visits each point exactly once and returns to the starting point, minimizing the total length. The visiting order of cities is decided by the solver; rotating or reversing a tour represents the same symmetric tour and does not affect validity.

All inputs in this experiment use TSPLIB-style `TYPE: TSP` and `EDGE_WEIGHT_TYPE: EUC_2D`. The distance between cities i and j is defined as:

```text
d(i,j) = floor(sqrt((xi-xj)^2 + (yi-yj)^2) + 0.5)
L = d(v1,v2) + ... + d(v[n-1],vn) + d(vn,v1)
```

Each edge length must be rounded individually before summing; do not accumulate floating-point Euclidean distances and round at the end. Python's `round` uses a different rule at half-integers and cannot be used directly in place of `floor(x+0.5)` here. Do not omit the final edge returning to the starting city.

Example input:

```text
NAME : rectangle4
TYPE : TSP
DIMENSION : 4
EDGE_WEIGHT_TYPE : EUC_2D
NODE_COORD_SECTION
1 0 0
2 1.5 0
3 1.5 1
4 0 1
EOF
```

City identifiers must follow the input file. The training manifest provides the number of cities, time budget, and hash for each instance.

Output uses the `TOUR_SECTION` format, where each city appears exactly once without repeating the starting city at the end:

```text
NAME : rectangle4_result
TYPE : TOUR
DIMENSION : 4
TOUR_SECTION
1
2
3
4
-1
EOF
```

In this example, the closed tour length is 6. Multiple city IDs per line separated by whitespace are allowed; `-1` or `EOF` terminates the tour. The checker independently verifies the node set, duplicates, and missing nodes, and recomputes the full closed tour length from the original coordinates without trusting the length self-reported by the solver.

The provided tools do not include tour construction, local search, crossover, or any other solving algorithm. Input parsing, data structures, algorithms, and output must be implemented from scratch.
