# Symmetric Euclidean traveling salesman problem

The solver returns a closed tour through every city. Edge lengths use the TSPLIB EUC_2D rounding rule.

## Instances

Development uses 33 instances of 5,087–19,402 cities. The 21 held-out instances contain 21,215–47,608 cities. Reference tour lengths used by the EAX launcher are in [runtime/reference_lengths.json](runtime/reference_lengths.json).

[Development files](train/) · [Held-out files](test/) · [Instance names, archive members and budgets](instances.csv) · [Problem documents](documents/)

## Agent materials

Read the task for the rules and allowed inputs, then the launch prompt for the instructions supplied to the agent. The problem directories preserve historical workspace names mentioned in those instructions. [Tasks](tasks/) · [Prompts](prompts/)

## Running a solver

Use GCC/G++ for the solver and Python 3 for the tour checker.

[Base algorithm](../../baselines/tsp/) · [Selected solvers](../../solvers/tsp/) · [Single-instance example](../../README.md#quick-start)

## Sources

The instances draw on TSPLIB and the Waterloo National and VLSI collections. The improvement task starts from Nagata’s GA-EAX Large implementation. [TSPLIB](https://doi.org/10.1287/ijoc.3.4.376) · [National TSP](https://www.math.uwaterloo.ca/tsp/world/countries.html) · [VLSI TSP](https://www.math.uwaterloo.ca/tsp/vlsi/index.html) · [GA-EAX license](documents/GA-EAX-LICENSE)
