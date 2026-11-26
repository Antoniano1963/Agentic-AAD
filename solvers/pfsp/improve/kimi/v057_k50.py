"""v057: more frequent stagnation kicks (K=50).

Builds on v056 (current best). One knob change: the stagnation threshold
drops from 100 to 100/2 = 50 iterations, doubling kick frequency. The
K=100 direction was positive (-0.0025), so probe denser triggering.
Contract: solve(processing, seed, time_limit) -> permutation of range(n).
"""
import math
import random
import time

DESTRUCTION = 4
TEMP_FACTOR = 0.8


class FastEnv:
    __slots__ = ('p', 'n', 'm', 'deadline', 'total', 'mean',
                 '_head', '_tail', '_vals', '_hF', '_tF', '_hW', '_tW')

    def __init__(self, processing, deadline):
        p = [list(map(int, row)) for row in processing]
        self.p = p
        self.n = len(p)
        self.m = len(p[0])
        self.deadline = deadline
        self.total = [sum(row) for row in p]
        self.mean = sum(self.total) / (self.n * self.m)
        n, m = self.n, self.m
        self._head = [[0] * m for _ in range(n + 1)]
        self._tail = [[0] * m for _ in range(n + 1)]
        self._vals = [0] * (n + 1)
        self._hF = [[0] * m for _ in range(n + 1)]
        self._tF = [[0] * m for _ in range(n + 1)]
        self._hW = [[0] * m for _ in range(n + 1)]
        self._tW = [[0] * m for _ in range(n + 1)]

    def expired(self):
        return time.monotonic() >= self.deadline

    def cost(self, seq):
        p = self.p
        m = self.m
        end = [0] * m
        for job in seq:
            row = p[job]
            t = 0
            for j in range(m):
                ej = end[j]
                t = t + row[j] if t >= ej else ej + row[j]
                end[j] = t
        return end[-1]

    def insert_costs(self, seq, job):
        p = self.p
        m = self.m
        head = self._head
        tail = self._tail
        k = len(seq)
        for i in range(k):
            prev = head[i]
            cur = head[i + 1]
            row = p[seq[i]]
            t = 0
            for j in range(m):
                pj = prev[j]
                t = t + row[j] if t >= pj else pj + row[j]
                cur[j] = t
        tk = tail[k]
        for j in range(m):
            tk[j] = 0
        for i in range(k - 1, -1, -1):
            after = tail[i + 1]
            cur = tail[i]
            row = p[seq[i]]
            t = 0
            for j in range(m - 1, -1, -1):
                aj = after[j]
                t = t + row[j] if t >= aj else aj + row[j]
                cur[j] = t
        vals = self._vals
        row = p[job]
        for i in range(k + 1):
            h = head[i]
            q = tail[i]
            t = 0
            value = 0
            for j in range(m):
                hj = h[j]
                t = t + row[j] if t >= hj else hj + row[j]
                s = t + q[j]
                if s > value:
                    value = s
            vals[i] = value
        return vals[:k + 1]

    def insert(self, seq, job):
        costs = self.insert_costs(seq, job)
        best = min(costs)
        at = costs.index(best)
        return seq[:at] + [job] + seq[at:], best

    def neh(self, rng=None, random_ties=False, order=None):
        if order is None:
            order = sorted(range(self.n), key=lambda j: -self.total[j])
        seq = []
        for at, job in enumerate(order):
            if self.expired():
                seq += order[at:]
                break
            if random_ties:
                costs = self.insert_costs(seq, job)
                b = min(costs)
                pos = rng.choice([i for i, v in enumerate(costs) if v == b])
                seq = seq[:pos] + [job] + seq[pos:]
            else:
                seq, _ = self.insert(seq, job)
        return seq, self.cost(seq)

    def rebuild(self, partial, removed):
        seq = partial[:]
        for at, job in enumerate(removed):
            if self.expired():
                seq += removed[at:]
                break
            seq, _ = self.insert(seq, job)
        return seq, self.cost(seq)

    def _build_ht(self, seq):
        p = self.p
        m = self.m
        hF = self._hF
        tF = self._tF
        n = len(seq)
        for i in range(n):
            prev = hF[i]
            cur = hF[i + 1]
            row = p[seq[i]]
            t = 0
            for j in range(m):
                pj = prev[j]
                t = t + row[j] if t >= pj else pj + row[j]
                cur[j] = t
        tn = tF[n]
        for j in range(m):
            tn[j] = 0
        for i in range(n - 1, -1, -1):
            after = tF[i + 1]
            cur = tF[i]
            row = p[seq[i]]
            t = 0
            for j in range(m - 1, -1, -1):
                aj = after[j]
                t = t + row[j] if t >= aj else aj + row[j]
                cur[j] = t

    def move_costs(self, seq, pos, job):
        """insert_costs(seq without seq[pos], job) via cached head/tail of seq."""
        p = self.p
        m = self.m
        hF = self._hF
        tF = self._tF
        hW = self._hW
        tW = self._tW
        k = len(seq) - 1
        prev = hF[pos]
        for i in range(pos + 1, k + 1):
            cur = hW[i]
            row = p[seq[i]]
            t = 0
            for j in range(m):
                pj = prev[j]
                t = t + row[j] if t >= pj else pj + row[j]
                cur[j] = t
            prev = cur
        after = tF[pos + 1]
        for i in range(pos - 1, -1, -1):
            cur = tW[i]
            row = p[seq[i]]
            t = 0
            for j in range(m - 1, -1, -1):
                aj = after[j]
                t = t + row[j] if t >= aj else aj + row[j]
                cur[j] = t
            after = cur
        vals = self._vals
        row = p[job]
        for i in range(k + 1):
            if i <= pos:
                h = hF[i]
                q = tF[i + 1] if i == pos else tW[i]
            else:
                h = hW[i]
                q = tF[i + 1]
            t = 0
            value = 0
            for j in range(m):
                hj = h[j]
                t = t + row[j] if t >= hj else hj + row[j]
                s = t + q[j]
                if s > value:
                    value = s
            vals[i] = value
        return vals[:k + 1]

    def descent(self, seq, rng):
        seq = seq[:]
        value = self.cost(seq)
        jobs = seq[:]
        rng.shuffle(jobs)
        self._build_ht(seq)
        for job in jobs:
            if self.expired():
                return seq, value
            pos = seq.index(job)
            costs = self.move_costs(seq, pos, job)
            b = min(costs)
            if b < value:
                at = costs.index(b)
                partial = seq[:pos] + seq[pos + 1:]
                seq = partial[:at] + [job] + partial[at:]
                value = b
                self._build_ht(seq)
        return seq, value


def solve(processing, seed, time_limit):
    rng = random.Random(seed)
    deadline = time.monotonic() + max(0.0, 0.995 * time_limit) - 0.02
    env = FastEnv(processing, deadline)

    starts = []
    s0, v0 = env.neh()
    starts.append((v0, s0))
    base_order = sorted(range(env.n), key=lambda j: -env.total[j])
    for _ in range(19):
        if env.expired():
            break
        order = base_order[:]
        i = 0
        while i < len(order) - 1:
            if rng.random() < 0.8:
                order[i], order[i + 1] = order[i + 1], order[i]
                i += 2
            else:
                i += 1
        alt, alt_value = env.neh(rng, random_ties=True, order=order)
        starts.append((alt_value, alt))
    starts.sort(key=lambda t: t[0])
    seq, value = None, None
    for raw_v, raw_s in starts[:3]:
        if env.expired():
            if seq is None:
                seq, value = raw_s, raw_v
            break
        des_s, des_v = env.descent(raw_s, rng)
        if seq is None or des_v < value:
            seq, value = des_s, des_v
    best, best_value = seq[:], value
    temperature = TEMP_FACTOR * env.mean / 10.0
    m = env.m
    crit = [[0] * m for _ in range(env.n + 1)]
    crit_cl = None

    stagnant = 0
    while not env.expired():
        d = min(DESTRUCTION, len(seq) - 1)
        if stagnant >= 50:
            selected = set(rng.sample(seq, d))
            partial = [j for j in seq if j not in selected]
            removed = [j for j in seq if j in selected]
            removed.sort(key=lambda j: -env.total[j])
            cand, cand_value = env.rebuild(partial, removed)
            cand, cand_value = env.descent(cand, rng)
            delta = cand_value - value
            if delta <= 0 or rng.random() < math.exp(-delta / temperature):
                seq, value = cand, cand_value
                crit_cl = None
                if value < best_value:
                    best, best_value = seq[:], value
            stagnant = 0
            continue
        if rng.random() < 0.9:
            if crit_cl is None:
                prev = crit[0]
                for i, job in enumerate(seq):
                    cur = crit[i + 1]
                    row = env.p[job]
                    t = 0
                    for j in range(m):
                        pj = prev[j]
                        t = t + row[j] if t >= pj else pj + row[j]
                        cur[j] = t
                    prev = cur
                critpos = set()
                i = len(seq)
                j = m - 1
                while i > 0:
                    up = crit[i - 1][j]
                    left = crit[i][j - 1] if j > 0 else -1
                    if up >= left:
                        i -= 1
                    else:
                        critpos.add(i - 1)
                        j -= 1
                crit_cl = list(critpos)
            if crit_cl:
                c = rng.choice(crit_cl)
                start = c - rng.randrange(d)
                start = max(0, min(start, len(seq) - d))
            else:
                start = rng.randrange(len(seq) - d + 1)
        else:
            start = rng.randrange(len(seq) - d + 1)
        removed = seq[start:start + d]
        partial = seq[:start] + seq[start + d:]
        removed.sort(key=lambda j: -env.total[j])
        cand, cand_value = env.rebuild(partial, removed)
        cand, cand_value = env.descent(cand, rng)
        delta = cand_value - value
        if delta <= 0 or rng.random() < math.exp(-delta / temperature):
            seq, value = cand, cand_value
            crit_cl = None
            if value < best_value:
                best, best_value = seq[:], value
        stagnant = 0 if delta < 0 else stagnant + 1
    return best
