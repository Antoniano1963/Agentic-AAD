# Prompt Provenance and Historical Notes

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

`GLM_reproduction_prompt.historical.md` is a post-hoc compiled reproduction prompt, not a verbatim export of GPT's full system prompt or session transcript at the time. The original constraints are grounded in the preserved protocols, configurations, source code, and round-by-round decision logs. This archive does not fabricate missing verbatim transcripts.

Known errata: the older reproduction document listed the IG final test budget as `0.03nm`; in reality, the `config.json` for the final tests of IG-99 and ALNS used `0.06nm`. Training used `0.03nm`. The phrase "read completed training feedback" in the older document must not be construed as providing GPT's full research history to new independent model experiments; whether historical priors are used must be declared separately.

Historical `ACTIVE` flags, PIDs, status text, and absolute script paths are contemporary records and must not be treated as current tasks or launch authorizations. Archiving operations do not launch any solver runs.
