"""v038 - v027 with fixed-cadence kick perturbations.

Hypothesis (eval 044, pre-registered): the failed v009 ladder spent sustained
phases at high d; rare single kicks are the untested alternative - every 400
stalled iterations one iteration destroys d=6 then returns to d=3
immediately. Supported if mean gap < 0.9889% beyond noise; falsified
otherwise.

Kernel: pfsp_math.py (frozen); hot insertion path is candidate-local arithmetic
(taillard head/tail, equivalence-checked against the kernel)."""
import math
import random
import time

from pfsp_math import PFSP

LAST_STATS = {}


def _insert_best(p, m, rev, seq, job, rng):
    """Insert `job` (not in `seq`) at its best position; ties broken randomly."""
    k = len(seq)
    prow = p[job]

    L = [[0] * m]
    prev = L[0]
    append_row = L.append
    for x in seq:
        t = 0
        cur = []
        ap = cur.append
        for pv, lv in zip(p[x], prev):
            if lv > t:
                t = lv
            t += pv
            ap(t)
        append_row(cur)
        prev = cur

    R = [[0] * m]
    nxt = R[0]
    append_row = R.append
    for x in reversed(seq):
        t = 0
        cur = []
        ap = cur.append
        for pv, rv in zip(rev[x], nxt[::-1]):
            if rv > t:
                t = rv
            t += pv
            ap(t)
        cur.reverse()
        append_row(cur)
        nxt = cur
    R.reverse()

    best = -1
    spots = []
    for i in range(k + 1):
        t = 0
        v = 0
        for lv, rv, pv in zip(L[i], R[i], prow):
            if lv > t:
                t = lv
            t += pv
            w = t + rv
            if w > v:
                v = w
        if best < 0 or v < best:
            best = v
            spots = [i]
        elif v == best:
            spots.append(i)
    at = rng.choice(spots)
    return seq[:at] + [job] + seq[at:], best


def _reinsert_best(p, m, rev, seq, job, rng):
    """Remove `job` from `seq` and reinsert it at its best position."""
    pos = seq.index(job)
    s = seq[:pos] + seq[pos + 1:]
    return _insert_best(p, m, rev, s, job, rng)


def _targeted_polish(p, m, rev, seq, val, rng, moved_jobs, f, max_sweeps=4):
    n = len(seq)
    targets = set(moved_jobs)
    for job in moved_jobs:
        pos = seq.index(job)
        for offset in (-6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6):
            targets.add(seq[(pos + offset) % n])
    order = list(targets)
    rng.shuffle(order)
    moves = 0
    for _ in range(max_sweeps):
        if f.expired():
            return seq, val, moves
        improved = False
        for job in order:
            if f.expired():
                return seq, val, moves
            seq, new_val = _reinsert_best(p, m, rev, seq, job, rng)
            if new_val <= val:
                if new_val < val:
                    improved = True
                val = new_val
            moves += 1
        if not improved:
            break
    return seq, val, moves


def solve(matrix, seed, time_limit):
    processing = [list(map(int, row)) for row in matrix]
    rng = random.Random(seed)
    deadline = time.monotonic() + 0.99 * time_limit
    f = PFSP(processing, deadline, rng)
    p = f.p
    m = f.m
    rev = [row[::-1] for row in p]

    seq, value = f.neh(random_ties=True)
    seq, value = f.descent(seq, passes=1)
    best, best_value = seq[:], value
    cur, cur_value = seq, value

    d = 3
    temp = 0.3 * f.mean
    stall_limit = 2 * f.n
    stall = 0
    iterations = 0
    restarts = 0
    polish_moves = 0

    while not f.expired():
        iterations += 1
        step_d = 6 if stall and stall % 400 == 0 else d
        partial, removed = f.remove(cur, step_d)
        removed.sort(key=lambda j: -f.total[j])
        cand = partial[:]
        cand_value = 0
        for job in removed:
            cand, cand_value = _insert_best(p, m, rev, cand, job, rng)
        cand, cand_value, mv = _targeted_polish(p, m, rev, cand, cand_value,
                                                rng, removed, f)
        polish_moves += mv
        if cand_value < cur_value or rng.random() < math.exp(-(cand_value - cur_value) / temp):
            cur, cur_value = cand, cand_value
        if cand_value < best_value:
            best, best_value = cand[:], cand_value
            stall = 0
        else:
            stall += 1
            if stall >= stall_limit:
                cur, cur_value = best[:], best_value
                stall = 0
                restarts += 1

    reported = f.cost(best)
    LAST_STATS.update(iterations=iterations, restarts=restarts,
                      polish_moves=polish_moves, best_makespan=reported)
    return best, reported
