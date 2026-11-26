# ROADEF/EURO 2018 · glass cutting

Given ordered stacks of requested pieces and glass plates with defects, choose a cutting pattern that meets the manufacturing constraints and minimizes waste.

## Instances

The 15 B instances are used for development and the 15 X instances for testing. Each instance has a batch file and a defects file. All share [the global cutting parameters](common/global_param.csv).

[Development files](train/) · [Held-out files](test/) · [Instance names, archive members and budgets](instances.csv) · [Problem documents](documents/)

## Agent materials

Read the task for the rules and allowed inputs, then the launch prompt for the instructions supplied to the agent. The problem directories preserve historical workspace names mentioned in those instructions. [Tasks](tasks/) · [Prompts](prompts/)

## Running a solver

The champion-derived variants require GCC/G++ and Boost Program Options (Ubuntu package `libboost-program-options-dev`). The included official checker executable runs on Linux x86-64.

[Base algorithm](../../baselines/roadef2018/) · [Selected solvers](../../solvers/roadef2018/) · [Single-instance example](../../README.md#quick-start)

## Sources

Saint-Gobain, ROADEF and EURO supplied the instances and checker. The improvement variants build on the challenge solver by Libralesso and Fontan. [Challenge materials](https://roadef.org/challenge/2018/) · [Original solver](https://github.com/fontanf/roadef2018)
