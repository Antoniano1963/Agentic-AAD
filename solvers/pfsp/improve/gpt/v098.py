"""Independent IG search with twelve-slot memory for larger instances."""
import random
import math
import time
from pfsp_math import PFSP
LAST_STATS = {}

def solve(matrix, seed, time_limit):
    global LAST_STATS
    begin = time.monotonic()
    rng = random.Random(seed)
    pf = PFSP(matrix, begin + .99*time_limit, rng)
    n, m = pf.n, pf.m
    high = 2*m >= n
    levels = (2,3,4,6) if n <= 100 else (3,4,6,8)
    rank = [float(t) for t in pf.total]
    if high:
        for j in range(n):
            mu = pf.total[j]/m
            rank[j] += m*math.sqrt(sum((t-mu)**2 for t in pf.p[j])/m)

    def scan(seq):
        seq = list(seq)
        objective = pf.cost(seq)
        schedule = seq[:]
        rng.shuffle(schedule)
        for j in schedule:
            if pf.expired():
                break
            old = seq.index(j)
            base = seq[:old]+seq[old+1:]
            values = pf.insert_costs(base,j)
            minimum = min(values)
            if minimum > objective:
                continue
            positions = [k for k,v in enumerate(values) if v == minimum]
            if minimum == objective and len(positions)>1:
                positions = [k for k in positions if k != old]
            at = rng.choice(positions)
            seq = base[:at]+[j]+base[at:]
            objective = minimum
        return seq,objective

    def pick_critical(seq, d):
        table = [[0]*(m+1) for _ in range(n+1)]
        for i,j in enumerate(seq,1):
            for k in range(1,m+1):
                table[i][k] = max(table[i-1][k],table[i][k-1])+pf.p[j][k-1]
        scores = dict.fromkeys(seq,1.0)
        i,k = n,m
        while i>0 and k>0:
            scores[seq[i-1]] += pf.p[seq[i-1]][k-1]
            if table[i-1][k] >= table[i][k-1]:
                i -= 1
            else:
                k -= 1
        left = seq[:]
        selected = set()
        for _ in range(d):
            draw = rng.random()*sum(scores[j] for j in left)
            chosen = left[-1]
            for j in left:
                draw -= scores[j]
                if draw <= 0:
                    chosen = j
                    break
            selected.add(chosen)
            left.remove(chosen)
        return selected

    seq, cost = pf.neh(random_ties=True)
    seq, cost = pf.descent(seq,passes=2,random_ties=True)
    record, record_cost = seq[:],cost
    thresholds = [cost]*(24 if n <= 100 else 12)
    iteration = accepted = improved = polished = polish_records = 0
    finishing = False
    before_polish = cost
    switch = .8 if 10*m <= n else (.95 if 5*m >= 2*n else .9)
    while not pf.expired():
        if not finishing and time.monotonic()-begin >= switch*time_limit:
            finishing = True
            seq,cost = record[:],record_cost
            before_polish = cost
        if finishing:
            seq,cost = scan(seq)
            polished += 1
            if cost < record_cost:
                record,record_cost = seq[:],cost
                polish_records += 1
            continue
        d = min(rng.choice(levels),n-1)
        if high and rng.random()<.5:
            pivot = rng.choice(seq)
            pool = [j for j in seq if j != pivot]
            rng.shuffle(pool)
            pool.sort(key=lambda j:sum(abs(a-b) for a,b in zip(pf.p[j],pf.p[pivot])))
            selected = set([pivot]+pool[:d-1])
        elif n <= 100 and 2*m < n and rng.random()<.5:
            selected = pick_critical(seq,d)
        else:
            selected = set(rng.sample(seq,d))
        keep = [j for j in seq if j not in selected]
        removed = [j for j in seq if j in selected]
        rng.shuffle(removed)
        removed.sort(key=lambda j:-rank[j])
        trial, trial_cost = pf.rebuild(keep,removed,random_ties=True)
        trial, trial_cost = scan(trial)
        if trial_cost < record_cost:
            record,record_cost = trial[:],trial_cost
            improved += 1
        slot = iteration%len(thresholds)
        if trial_cost <= cost or trial_cost <= thresholds[slot]:
            seq,cost = trial,trial_cost
            accepted += 1
        thresholds[slot] = cost
        iteration += 1
    LAST_STATS = dict(iterations=iteration,accepted=accepted,improvements=improved,
                      polish_passes=polished,polish_records=polish_records,
                      polish_gain=before_polish-record_cost if finishing else 0,
                      elapsed=time.monotonic()-begin)
    return record,record_cost
