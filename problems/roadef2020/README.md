# ROADEF/EURO 2020 · grid maintenance

Assign a start time to each maintenance intervention while respecting resource capacities and exclusions. The objective combines expected risk and a quantile-based risk term.

## Instances

The 15 C development instances and 15 X test instances are distributed in the original 7-Zip archives. The single-instance runner extracts the selected JSON file. The checker directory also contains two small examples from RTE.

[Development files](train/) · [Held-out files](test/) · [Instance names, archive members and budgets](instances.csv) · [Problem documents](documents/)

## Agent materials

Read the task for the rules and allowed inputs, then the launch prompt for the instructions supplied to the agent. The problem directories preserve historical workspace names mentioned in those instructions. [Tasks](tasks/) · [Prompts](prompts/)

## Running a solver

Use GCC/G++ with OpenMP and Python with NumPy. Install 7-Zip or bsdtar to open the full instance archives. The two official examples can be run directly.

[Base algorithm](../../baselines/roadef2020/) · [Selected solvers](../../solvers/roadef2020/) · [Single-instance example](../../README.md#quick-start)

## Original ALNS and agent improvements

The improvement experiments all start from the same [original ALNS](../../baselines/roadef2020/). Kimi-code, Zcode and DeepSeek Harness each have a complete source tree. To compile a variant, enter its directory and run `bash build.sh`. This example builds the Kimi-code solver from the repository root.

```bash
bash solvers/roadef2020/improve/kimi/build.sh
```

[Kimi-code](../../solvers/roadef2020/improve/kimi/) · [Zcode](../../solvers/roadef2020/improve/glm/) · [DeepSeek Harness](../../solvers/roadef2020/improve/deepseek/)

The [OpenEvolve workflow candidate](../../solvers/roadef2020/frameworks/openevolve-deepseek/) has its own standalone source and build script.

The original ALNS and the Kimi-code, Zcode and OpenEvolve variants expect a numeric `ComputationTime` field. RTE’s two small checker examples omit it. Use an instance from the C or X archive with those implementations. The scratch Zcode solver accepts the small example directly. Run this from the repository root with NumPy installed.

```bash
bash solvers/roadef2020/from_scratch/glm/build.sh
python3 tools/run_instance.py --solver roadef2020/from_scratch/glm --split example --instance example1 --seconds 12 --seed 1111 --out runs/grid-example
```

## Sources

RTE supplies the instance archives, specification, and Python checker. The improved solver uses the ALNS implementation of Woller, Rada and Kulich. [Official data and checker](https://github.com/rte-france/challenge-roadef-2020) · [Upstream ALNS](https://github.com/wolledav/ROADEF_2020)
