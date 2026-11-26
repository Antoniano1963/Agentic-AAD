# EAX training requirements

> **Translation note:** This file is an English translation of the original Chinese task instruction provided to the LLM agent during the experiments.

User-confirmed protocol (2026-09-10):
- Training set: supplied instances with 5000 <= n <= 20000, explicitly excluding usa13509; 33 instances (6 Easy, 27 Medium).
- During improvement/training, access only these training instances. Do not access non-training instances or use their results to guide design.
- Shared wall-clock budget per independent evaluation: 900 seconds for 5000 <= n <= 10000; 1200 seconds for 10000 < n <= 20000. No supplied instance lies exactly at n=10000 or n=20000.
- At most 5 restarts after the initial search (at most 6 searches). All restarts, initialization and search share the one total time budget.
- 10 independent repetitions per instance, with reproducible distinct seeds.
- Primary metric: mean gap. Per-instance mean is the mean final best-so-far gap across its 10 repetitions. Overall training score is the equally weighted mean of the per-instance means.
- Gap (%) = 100 * (tour length - BKS) / BKS.
- One full training evaluation consists of 33 * 10 = 330 runs. Do not silently omit failures.
- usa13509 is explicitly excluded by the user from training. Do not access or evaluate it during algorithm improvement. Resolving its prior anomaly is not a prerequisite for this training task.
- This instruction records requirements only; it does not authorize starting algorithm evolution or launching evaluations now.

## Algorithm implementation requirements

Additional user-confirmed requirements:
- Every improved version must start from scratch (original Chinese user wording: "start from 0"). Preserve this requirement for implementation; do not treat permission to learn from evaluation feedback as permission to copy prior candidate solutions.
- Store improved versions on cpu3 under /data/CodexEvo/EAXEvo/history/.
- Give each version its own directory. Include complete source and build instructions so that it compiles independently without relying on another version's directory.
- Follow feedback-driven iterative improvement, as requested for IG: inspect prior training evaluation feedback and use it to design subsequent code. Do not substitute a fixed batch of predesigned candidates for this adaptive process.
- Preserve each evaluated version and its associated evaluation configuration/results so feedback is traceable.
- These instructions record requirements only; no evolution or evaluations are being started.


## Active evolution authorization
The user has now authorized 50 complete training evaluations and requested feedback-driven improvement until all 50 are used. This supersedes the earlier requirements-only/no-start notes.
Server state: /data/CodexEvo/EAXEvo/history/ledger.json is the evaluation ledger; /data/CodexEvo/EAXEvo/history/protocol.json and training_manifest.json fix the protocol and 33-instance allowlist. Each evaluation contains 330 runs. v001 is the same-budget control baseline, counted as evaluation 1. Do not read held-out datasets or outcomes. Each next version must be designed after completed training feedback, created independently from history/frozen_base using create_version.py and explicit documented edits. Never copy a previous candidate's source or load stored solutions as search initialization.
The first evaluation was launched with history/launch_evaluation.py 1; supervisor PID 458633, runner PID 458635. Verify live server state before acting; these PIDs are historical handles, not proof of continued execution. A failed validation requires audit, not silent exclusion or automatic replay. Completed scores are recorded by the supervisor only after all 330 results pass. The 50-evaluation objective remains active.

## Monitoring cadence (user update)
Use an 11-minute monitoring interval. Avoid repeated minute-level progress polls and unchanged status messages. An EAX heartbeat was requested to wake this thread; active goal continuations should not duplicate its checks. While a verified evaluation remains live and no action is due, wait for the next scheduled check. Continue design immediately after a complete feedback/audit is available. Do not mark the 50-evaluation goal complete or blocked merely because a run is in progress.
