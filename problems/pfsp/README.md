# Permutation flow-shop scheduling

A single permutation gives the order in which all jobs visit the machines. The objective is the makespan, the time at which the last job finishes on the last machine.

## Instances

The development split uses 100 or 200 jobs. The held-out split uses 300 through 800 jobs. Each size is paired with 20, 40 or 60 machines. The text files give machine identifiers and processing times.

[Development files](train/) · [Held-out files](test/) · [Instance names, archive members and budgets](instances.csv) · [Problem documents](documents/)

## Agent materials

Read the task for the rules and allowed inputs, then the launch prompt for the instructions supplied to the agent. The problem directories preserve historical workspace names mentioned in those instructions. [Tasks](tasks/) · [Prompts](prompts/)

## Running a solver

The C++ scratch solvers use GCC/G++. The Python improvement and workflow solvers use NumPy.

[Base algorithm](../../baselines/pfsp/) · [Selected solvers](../../solvers/pfsp/) · [Single-instance example](../../README.md#quick-start)

## Sources

The instances are from the Vallada, Ruiz and Framinan benchmark. [Benchmark paper](https://doi.org/10.1016/j.ejor.2014.07.033)
