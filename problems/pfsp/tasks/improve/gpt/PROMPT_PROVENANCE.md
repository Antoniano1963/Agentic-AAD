# Prompt and Historical Notes

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

`GLM_reproduction_prompt.historical.md` is a post-hoc reproduction prompt compiled after the fact, not a verbatim export of GPT's full system prompt or conversation at the time. Original constraints are governed by the preserved protocols, configurations, source code, and round-by-round decision records. The archive does not fabricate missing verbatim transcripts.

Known errata: the old reproduction document wrote the IG final test budget as `0.03nm`; in reality, `config.json` for the final IG-99 and ALNS tests used `0.06nm`, while training used `0.03nm`. The phrase "reading completed training feedback" in the old document must not be interpreted as supplying GPT's entire research history to new independent model experiments; whether historical priors are used must be declared separately.

Historical `ACTIVE` flags, PIDs, status strings, and absolute script paths are records from that time and must not be treated as current tasks or launch authorizations. The current archival process does not launch any solver runs.
