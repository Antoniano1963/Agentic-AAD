# User Authorization: Completing Prior Proposals to Accumulate Prior Knowledge

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

Latest user requirement: finish testing all 39 prior proposals first to serve as prior knowledge, then continuously improve based on feedback.

This batch resumes the unexecuted original proposals `v006–v039`. They are pre-written independent proposals created earlier and are not presented as new designs generated from the latest results. They reference the completed original IG evaluation 1 as the training control, and the purpose of executing them is determined by this user authorization.

Existing complete results for `v001–v005` and the original IG are retained; the already-launched `v040` control finishes normally and also counts against the budget. After 41 full evaluations in total, wait for feedback analysis. The remaining 59 rounds will not be pre-filled with candidates or automatically scheduled for re-evaluation.

All constraints on data isolation, 600 solves per full evaluation, time budgets, 400-way parallelism, and Python-only implementation remain unchanged.
