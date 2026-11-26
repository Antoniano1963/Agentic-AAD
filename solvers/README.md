# Solver implementations

Compare the [five base algorithms](../baselines/) with the final candidates listed below. Each candidate directory includes its own build and run instructions.

## Coding agents

| Problem | Task | Codex | Kimi-code | Zcode | DeepSeek Harness |
| --- | --- | --- | --- | --- | --- |
| [PFSP](../problems/pfsp/) | Improve | [v098](pfsp/improve/gpt/) | [v057_k50](pfsp/improve/kimi/) | [v038_kick](pfsp/improve/glm/) | [v029_double_perturb](pfsp/improve/deepseek/) |
| [PFSP](../problems/pfsp/) | From scratch | — | [v047](pfsp/from_scratch/kimi/) | [v009](pfsp/from_scratch/glm/) | [v047](pfsp/from_scratch/deepseek/) |
| [TSP](../problems/tsp/) | Improve | [v026](tsp/improve/gpt/) | [v013_pop200L](tsp/improve/kimi/) | [v021_ent035f40](tsp/improve/glm/) | [v026_ils](tsp/improve/deepseek/) |
| [TSP](../problems/tsp/) | From scratch | — | [v152](tsp/from_scratch/kimi/) | [v014](tsp/from_scratch/glm/) | [v039](tsp/from_scratch/deepseek/) |
| [ROADEF 2018](../problems/roadef2018/) | Improve | — | [v015](roadef2018/improve/kimi/) | [iter020 + iter016](roadef2018/improve/glm/) | [Selected configuration](roadef2018/improve/deepseek/) |
| [ROADEF 2018](../problems/roadef2018/) | From scratch | — | [Selected source](roadef2018/from_scratch/kimi/) | [Selected source](roadef2018/from_scratch/glm/) | [Selected source](roadef2018/from_scratch/deepseek/) |
| [ROADEF 2020](../problems/roadef2020/) | Improve | — | [v045](roadef2020/improve/kimi/) | [cand_07_v8](roadef2020/improve/glm/) | [v1_streaming_repair](roadef2020/improve/deepseek/) |
| [ROADEF 2020](../problems/roadef2020/) | From scratch | — | [Selected source](roadef2020/from_scratch/kimi/) | [Selected source](roadef2020/from_scratch/glm/) | [Selected source](roadef2020/from_scratch/deepseek/) |
| [ROADEF 2022](../problems/roadef2022/) | Improve | — | [v011](roadef2022/improve/kimi/) | [v031](roadef2022/improve/glm/) | [v062](roadef2022/improve/deepseek/) |
| [ROADEF 2022](../problems/roadef2022/) | From scratch | — | [v046](roadef2022/from_scratch/kimi/) | [v014](roadef2022/from_scratch/glm/) | [v007](roadef2022/from_scratch/deepseek/) |

The ROADEF 2018 Zcode improvement pairs the iter020 wrapper with the source associated with iter016. Its correspondence to the submitted executable remains unverified. [Build notes](roadef2018/improve/glm/)

## Fixed-workflow candidates

| Problem | ReEvo | OpenEvolve |
| --- | --- | --- |
| PFSP | [Selected operator](pfsp/frameworks/reevo-deepseek/) | [round046](pfsp/frameworks/openevolve-deepseek/) |
| TSP | — | [round038](tsp/frameworks/openevolve-deepseek/) |
| ROADEF 2018 | — | [Selected source](roadef2018/frameworks/openevolve-deepseek/) |
| ROADEF 2020 | — | [Selected source](roadef2020/frameworks/openevolve-deepseek/) |
