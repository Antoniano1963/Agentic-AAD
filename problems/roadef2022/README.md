# ROADEF/EURO 2022 · truck loading

Choose how to form stacks, load trucks and time shipments. A feasible plan must satisfy geometric and axle-load constraints. The objective combines transportation and inventory cost.

## Instances

There are 50 C development instances and 105 X test instances. Each archive member contains item, truck and parameter CSV inputs. The same short identifier can refer to different data across splits.

[Development files](train/) · [Held-out files](test/) · [Instance names, archive members and budgets](instances.csv) · [Problem documents](documents/)

## Agent materials

Read the task for the rules and allowed inputs, then the launch prompt for the instructions supplied to the agent. The problem directories preserve historical workspace names mentioned in those instructions. [Tasks](tasks/) · [Prompts](prompts/)

## Running a solver

Use a Java runtime for the official checker. S46 variants also need GCC/G++ with OpenMP and the Python packages in [requirements.txt](../../requirements.txt). Activate that environment before launching the solver.

[Base algorithm](../../baselines/roadef2022/) · [Selected solvers](../../solvers/roadef2022/) · [Single-instance example](../../README.md#quick-start)

## Sources

Renault, ROADEF and EURO supplied the challenge materials. The improvement setting uses the study’s HiGHS port of Team S46’s matheuristic. [Official materials](https://github.com/renault-iaa/challenge-roadef-2022) · [S46 source](https://github.com/FranciscoParrenoTorres/Roadef2022/tree/6d14ae47819ba119e1a75ae0cbdc404a2c06d4d5)
