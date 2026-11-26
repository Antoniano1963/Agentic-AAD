"""v029: two perturbation rounds per descent pass.

Candidate under test versus v007_warm_start.py (reference; three-stream mean
1.694353%, se 0.0050pp).

Why this idea: the phase-cost probe showed the descent pass accounts for essentially
the entire round cost (1.150s of a 1.250s round at 200x60, and 0.104s vs 0.004s at
100x20), which makes local repair the scarce resource. Round 3 spent *more* on repair
(driving descent to convergence) and lost 0.071pp; the subset idea (repair less of
the sequence) was screened and came out worse. The untested trade is the opposite:
perturb the structure twice and repair it once, so a single descent amortises over a
larger structural change.

Screening evidence: on the fixed 6-instance subset at a reduced budget, this variant
scored 2.01080 against the reference's 2.02459 -- the only one of four screened ideas
that did not lose to the reference (alternating scales 2.14795, swap probe 2.17540,
combination 2.39857). The subset gap of -0.014 is well inside screening noise
(sd about 0.1pp), so this official round is the real test of the idea -- and also a
test of whether the screening harness can predict official outcomes.

Change (one lever only, relative to v007): each round performs PAIRS = 2 successive
destroy-rebuild steps before the single descent pass. Warm start keeps its frozen
configuration (WARM_FRACTION = 0.05, WARM_MAX_ATTEMPTS = 3, randomized-order
constructions). Everything else identical: DESTRUCTION = 4, random removal,
random-order reconstruction, fixed simulated-annealing temperature
0.4 * mean / 10, stopping just before the deadline.

Contract: solve(processing, seed, time_limit) -> permutation of range(n).
Pure Python, standard library only, no external files, no instance identity,
no environment access, stops before 0.99 * time_limit.
"""
import math
import random
import time

from pfsp_math import PFSP

DESTRUCTION = 4
TEMP_FACTOR = 0.4
WARM_FRACTION = 0.05
WARM_MAX_ATTEMPTS = 3
PAIRS = 2


def solve(processing, seed, time_limit):
    rng = random.Random(seed)
    budget = max(0.0, 0.99 * time_limit) - 0.02
    start = time.monotonic()
    deadline = start + budget
    env = PFSP(processing, deadline, rng)

    seq, value = env.neh()
    seq, value = env.descent(seq)
    best, best_value = seq[:], value
    warm_deadline = start + WARM_FRACTION * budget
    attempts = 1
    while attempts < WARM_MAX_ATTEMPTS and time.monotonic() < warm_deadline:
        order = list(range(env.n))
        rng.shuffle(order)
        cand, cand_value = env.neh(order=order, random_ties=True)
        cand, cand_value = env.descent(cand)
        attempts += 1
        if cand_value < best_value:
            best, best_value = cand[:], cand_value
    seq, value = best[:], best_value

    temperature = TEMP_FACTOR * env.mean / 10.0
    while not env.expired():
        cand, cand_value = seq[:], value
        for _ in range(PAIRS):
            partial, removed = env.remove(cand, DESTRUCTION)
            cand, cand_value = env.rebuild(partial, removed)
            if env.expired():
                break
        cand, cand_value = env.descent(cand)
        delta = cand_value - value
        if delta <= 0 or rng.random() < math.exp(-delta / temperature):
            seq, value = cand, cand_value
            if value < best_value:
                best, best_value = seq[:], value
    return best
