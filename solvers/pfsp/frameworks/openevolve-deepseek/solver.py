import numpy as np
from typing import List, Tuple
import math
import random
import time
import os
import sys
from pathlib import Path

try:
    from .._sequence_validation import validate_destroy_result, validate_solution_sequence
except (ImportError, ValueError):
    problems_dir = str(Path(__file__).resolve().parents[1])
    if problems_dir not in sys.path:
        sys.path.insert(0, problems_dir)
    from _sequence_validation import validate_destroy_result, validate_solution_sequence

os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib")


def read_instance(file_path):
    """读取实例文件"""
    with open(file_path, 'r') as f:
        # 读取第一行获取作业数和机器数
        n_jobs, n_machines = map(int, f.readline().strip().split())

        # 初始化处理时间矩阵
        processing_times = np.zeros((n_jobs, n_machines), dtype=int)

        # 读取每个工件的处理时间
        for i in range(n_jobs):
            # 读取一行数据，每行包含机器-时间对
            pairs = list(map(int, f.readline().strip().split()))
            # 将机器-时间对转换为处理时间数组
            for j in range(0, len(pairs), 2):
                machine = pairs[j]
                time = pairs[j + 1]
                processing_times[i][machine] = time

    return processing_times, n_jobs, n_machines


class IG(object):
    def __init__(self, processing_times: np.ndarray, n_jobs: int, n_machines: int,
                 d_max: int = 4, max_iterations: int = 50):
        self.processing_times = processing_times
        # Pure-Python list-of-lists copy: far faster than NumPy for the tiny
        # per-insertion work that dominates the IG loop.
        if hasattr(processing_times, "tolist"):
            self.p = processing_times.tolist()
        else:
            self.p = [list(map(int, row)) for row in processing_times]
        self.n_jobs = int(n_jobs)
        self.n_machines = int(n_machines)
        self.d_max = d_max
        self.max_iterations = max_iterations

    def calculate_objectives(self, sequence: List[int]) -> Tuple[float, List[List[Tuple[float, float]]]]:
        """计算makespan和调度信息，只在算法的最终结果返回时使用"""
        # 直接调用calculate_makespan计算makespan
        makespan = self.calculate_makespan(sequence)

        # 以下仅计算详细调度信息
        schedules = []
        machine_completion = [0] * self.n_machines

        for job in sequence:
            job_schedule = []
            for m in range(self.n_machines):
                start_time = machine_completion[m]
                if m > 0:
                    start_time = max(start_time, job_schedule[m - 1][1])
                end_time = start_time + self.processing_times[job][m]
                job_schedule.append((start_time, end_time))
                machine_completion[m] = end_time
            schedules.append(job_schedule)

        return makespan, schedules

    def calculate_makespan(self, sequence: List[int]) -> int:
        """计算makespan - pure Python lists for speed on small arrays."""
        m = self.n_machines
        if m == 0:
            return 0
        p = self.p
        comp = [0] * m
        for job in sequence:
            pt = p[job]
            prev = 0
            for j in range(m):
                v = comp[j]
                if prev > v:
                    v = prev
                v += pt[j]
                comp[j] = v
                prev = v
        return comp[m - 1]

    def _forward(self, seq):
        m = self.n_machines
        p = self.p
        k = len(seq)
        E = [[0] * (m + 1) for _ in range(k + 1)]
        for i in range(1, k + 1):
            job = seq[i - 1]
            prev = E[i - 1]
            cur = E[i]
            pt = p[job]
            left = 0
            for j in range(1, m + 1):
                v = prev[j]
                if left > v:
                    v = left
                left = v + pt[j - 1]
                cur[j] = left
        return E

    def _backward(self, seq):
        m = self.n_machines
        p = self.p
        k = len(seq)
        Q = [[0] * (m + 2) for _ in range(k + 1)]
        for i in range(k - 1, -1, -1):
            job = seq[i]
            nxt = Q[i + 1]
            cur = Q[i]
            pt = p[job]
            right = 0
            for j in range(m, 0, -1):
                v = nxt[j]
                if right > v:
                    v = right
                right = v + pt[j - 1]
                cur[j] = right
        return Q

    def _best_insertion(self, seq, job, E=None, Q=None):
        if E is None:
            E = self._forward(seq)
        if Q is None:
            Q = self._backward(seq)
        m = self.n_machines
        pt = self.p[job]
        k = len(seq)
        best_ms = 10**18
        best_pos = 0
        for pos in range(k + 1):
            e = E[pos]
            q = Q[pos]
            left = 0
            ms = 0
            for j in range(1, m + 1):
                v = e[j]
                if left > v:
                    v = left
                left = v + pt[j - 1]
                val = left + q[j]
                if val > ms:
                    ms = val
            if ms < best_ms:
                best_ms = ms
                best_pos = pos
            elif ms == best_ms and random.random() < 0.2:
                best_pos = pos
        return best_pos, best_ms

    def neh(self, randomized=False):
        n = self.n_jobs
        p = self.p
        if randomized:
            order = sorted(range(n), key=lambda job: (-sum(p[job]), random.random()))
        else:
            order = sorted(range(n), key=lambda job: -sum(p[job]))
        seq = []
        for job in order:
            pos, _ = self._best_insertion(seq, job)
            seq.insert(pos, job)
        return seq

    def local_search(self, sequence, deadline, check=None, current=None):
        seq = sequence[:]
        n = len(seq)
        if current is None:
            current = self.calculate_makespan(seq)
        if n < 2:
            return seq, current
        forward = self._forward
        backward = self._backward
        best_ins = self._best_insertion
        now = time.time
        # ``check[job]`` is True when that job's best re-insertion may have
        # changed.  Seeding it with only the perturbed jobs (+ neighbours) makes
        # one local-search pass cost O(d) rather than O(n): this is the single
        # biggest throughput win of the IG loop, because the vast majority of
        # jobs were already locally optimal before the destruction step.
        if check is None:
            check = [True] * n
        while True:
            jobs = [job for job in seq if check[job]]
            if not jobs:
                break
            random.shuffle(jobs)
            improved = False
            for job in jobs:
                if now() >= deadline:
                    return seq, current
                if not check[job]:
                    continue
                idx = seq.index(job)
                base = seq[:idx] + seq[idx + 1:]
                E = forward(base)
                Q = backward(base)
                pos, ms = best_ins(base, job, E, Q)
                check[job] = False
                if ms < current:
                    base.insert(pos, job)
                    lo = min(idx, pos)
                    hi = max(idx, pos)
                    for k in range(lo, hi + 1):
                        check[base[k]] = True
                    check[job] = False
                    seq = base
                    current = ms
                    improved = True
            if not improved:
                break
        return seq, current

    def _construct(self, remaining, removed):
        # Return the exact makespan too: the last best-insertion evaluation
        # already yields it, so callers avoid a redundant O(n*m) evaluation.
        seq = remaining[:]
        ms = 0
        for job in removed:
            pos, ms = self._best_insertion(seq, job)
            seq.insert(pos, job)
        return seq, ms

    def _compute_forward_times(self, sequence):
        """计算前向完成时间 E[i][j] = 前i个工件在机器j上的完成时间"""
        k = len(sequence)
        m = self.n_machines
        E = np.zeros((k + 1, m + 1))  # E[0][j] = 0, E[i][0] = 0

        for i in range(1, k + 1):
            job = sequence[i - 1]  # 第i个位置的工件（0-based索引）
            for j in range(1, m + 1):
                # E[i][j] = max(前一个工件在当前机器的完成时间, 当前工件在前一台机器的完成时间) + 当前处理时间
                E[i][j] = max(E[i - 1][j], E[i][j - 1]) + self.processing_times[job][j - 1]
        return E

    def _compute_backward_times(self, sequence):
        """计算反向尾部时间 Q[i][j] = 从位置i开始到序列结束在机器j上需要的时间"""
        k = len(sequence)
        m = self.n_machines
        # Q[k+1][j] = 0 (序列结束), Q[i][m+1] = 0 (最后一台机器之后)
        Q = np.zeros((k + 2, m + 2))

        # 从后往前计算
        for i in range(k, 0, -1):
            job = sequence[i - 1]  # 位置i对应的工件
            for j in range(m, 0, -1):
                # Q[i][j] = max(下一个位置在当前机器的时间, 当前位置在下一台机器的时间) + 当前处理时间
                Q[i][j] = max(Q[i + 1][j], Q[i][j + 1]) + self.processing_times[job][j - 1]
        return Q

    def _insert_and_evaluate(self, E, Q, sequence, job_to_insert, insert_pos):
        """评估在指定位置插入工件后的makespan"""
        m = self.n_machines
        # F[j] = 插入工件在机器j上的完成时间
        F = np.zeros(m + 1)

        # 计算插入工件在每台机器上的完成时间
        for j in range(1, m + 1):
            # max(前面工件在当前机器的完成时间, 当前工件在前一台机器的完成时间) + 处理时间
            F[j] = max(E[insert_pos][j], F[j - 1]) + self.processing_times[job_to_insert][j - 1]

        # 计算makespan = max(插入工件的完成时间 + 后续工件的尾部时间)
        makespan = 0
        for j in range(1, m + 1):
            # 添加边界检查
            if insert_pos + 1 < Q.shape[0]:
                makespan = max(makespan, F[j] + Q[insert_pos + 1][j])
            else:
                makespan = max(makespan, F[j])  # 如果插在最后，没有尾部时间
        return makespan

    def neh_heuristic(self) -> Tuple[List[int], float]:
        """使用NEH启发式算法"""
        # 按总处理时间降序排列作业
        job_order = np.argsort(-self.processing_times.sum(axis=1)).tolist()
        sequence = [job_order[0]]

        # 初始序列只有一个工件，计算其makespan
        final_makespan = self.calculate_makespan(sequence)

        for k in range(1, self.n_jobs):
            job = job_order[k]
            best_makespan = float('inf')
            best_position = 0

            # 计算当前序列的前向完成时间和反向尾部时间
            E = self._compute_forward_times(sequence)
            Q = self._compute_backward_times(sequence)

            # 尝试所有可能的插入位置
            for i in range(len(sequence) + 1):
                ms = self._insert_and_evaluate(E, Q, sequence, job, i)
                if ms < best_makespan:
                    best_makespan = ms
                    best_position = i

            sequence.insert(best_position, job)
            final_makespan = best_makespan  # 更新最终makespan

        return sequence, final_makespan

    def destruction_phase(self, sequence: List[int]) -> Tuple[List[int], List[int]]:
        """破坏阶段：随机移除d_max个工件"""
        # 固定破坏 d_max 个工件
        d = self.d_max
        # 确保破坏数量不超过序列长度
        d = min(d, len(sequence))
        # 随机选择 d 个位置
        positions = random.sample(range(len(sequence)), d)
        # 移除选中的工件
        removed_jobs = [sequence[p] for p in sorted(positions, reverse=True)]
        remaining_sequence = sequence.copy()
        for p in sorted(positions, reverse=True):
            remaining_sequence.pop(p)
        return remaining_sequence, removed_jobs

    def construction_phase(self, sequence: List[int], candidates: List[int], start_time=None, actual_time_limit=None, fallback_sequence=None) -> List[int]:
        """
        构造阶段：使用插入法重新构建序列，增加时间检查。
        超时时直接返回 fallback_sequence（即破坏前的完整解）。
        """
        seq = sequence.copy()
        for job in candidates:
            # 时间检查
            if actual_time_limit is not None and start_time is not None:
                if (time.time() - start_time) >= actual_time_limit:
                    # 超时，直接返回破坏前的完整解
                    return fallback_sequence if fallback_sequence is not None else seq
            best_makespan = float('inf')
            best_position = 0
            E = self._compute_forward_times(seq)
            Q = self._compute_backward_times(seq)
            for i in range(len(seq) + 1):
                ms = self._insert_and_evaluate(E, Q, seq, job, i)
                if ms < best_makespan:
                    best_makespan = ms
                    best_position = i
            seq.insert(best_position, job)
        return seq

    def local_search_insertion(self, sequence: List[int], start_time=None, actual_time_limit=None) -> List[int]:
        seq = sequence.copy()
        E = self._compute_forward_times(seq)
        current_makespan = E[len(seq)][self.n_machines]
        for i in range(len(seq)):
            # 时间检查
            if actual_time_limit is not None and start_time is not None:
                if (time.time() - start_time) >= actual_time_limit:
                    return seq  # 这里 seq 一定是完整的
            job = seq[i]
            seq.pop(i)
            E_removed = self._compute_forward_times(seq)
            Q_removed = self._compute_backward_times(seq)
            best_pos = i
            best_makespan = current_makespan
            for j in range(len(seq) + 1):
                makespan = self._insert_and_evaluate(E_removed, Q_removed, seq, job, j)
                if makespan < best_makespan:
                    best_makespan = makespan
                    best_pos = j
            seq.insert(best_pos, job)
            if best_makespan < current_makespan:
                return seq
        return seq

    def solve(self, start_time=None, time_limit=None, instance_name="unknown"):
        if start_time is None:
            start_time = time.time()
        if time_limit is None:
            time_limit = self.n_jobs * self.n_machines * 0.03
        deadline = start_time + max(0.001, time_limit * 0.985 - 0.01)
        total_budget = max(1e-3, deadline - start_time)
        # Tail of the budget is spent intensifying around the incumbent.
        intens_start = start_time + total_budget * 0.72
        # Keep a small slice for one final full local-search polish.
        loop_deadline = deadline - total_budget * 0.02
        # Reserve only ~5% of the budget for NEH restarts.
        restart_deadline = min(deadline, start_time + total_budget * 0.05)

        # --- Initialization: best of several (randomized) NEH restarts ---
        best_init = None
        best_init_ms = 10**18
        seq = self.neh()
        ms = self.calculate_makespan(seq)
        if ms < best_init_ms:
            best_init, best_init_ms = seq, ms
        for _ in range(20):
            if time.time() >= restart_deadline:
                break
            seq = self.neh(randomized=True)
            ms = self.calculate_makespan(seq)
            if ms < best_init_ms:
                best_init, best_init_ms = seq, ms
        seq = best_init
        cur = best_init_ms

        if time.time() < deadline:
            seq, cur = self.local_search(seq, deadline)

        best = seq[:]
        best_ms = cur

        total_pt = 0
        for row in self.p:
            total_pt += sum(row)
        temp0 = 0.4 * total_pt / (max(1, self.n_jobs * self.n_machines) * 10.0)
        if temp0 < 1e-9:
            temp0 = 1e-9

        n = len(best)
        if n <= 1:
            return best, best_ms, None

        base_d = max(4, min(8, n // 60))
        stagnation = 0
        stag_limit = max(50, n // 2)
        intens_done = False

        while True:
            now = time.time()
            if now >= loop_deadline:
                break
            # Linear cooling: exploration early, exploitation late.
            frac = min(1.0, (now - start_time) / total_budget)
            temp = temp0 * (1.0 - 0.65 * frac)
            if temp < 1e-9:
                temp = 1e-9

            in_intens = now >= intens_start
            if in_intens and not intens_done:
                seq = best[:]
                cur = best_ms
                stagnation = 0
                intens_done = True

            if stagnation >= stag_limit:
                # Escape plateau: restart from incumbent with an extra-large
                # destruction (large during exploration, small when intensifying).
                seq = best[:]
                cur = best_ms
                stagnation = 0
                if in_intens:
                    dd = min(n - 1, base_d + random.randint(1, 3))
                    force_accept = False
                else:
                    dd = min(n - 1, base_d + random.randint(6, 10))
                    force_accept = True
            else:
                if in_intens:
                    dd = base_d if random.random() < 0.85 else min(n - 1, base_d + 2)
                else:
                    if random.random() < 0.7:
                        dd = base_d
                    else:
                        dd = random.randint(base_d + 1, min(base_d + 4, n - 1))
                force_accept = False
            dd = min(dd, n - 1)
            if dd <= 0:
                break

            positions = random.sample(range(n), dd)
            pos_set = set(positions)
            remaining = [seq[i] for i in range(n) if i not in pos_set]
            removed = [seq[i] for i in positions]
            random.shuffle(removed)

            candidate, cand_ms = self._construct(remaining, removed)
            # Seed the dirty mask with the perturbed jobs and their immediate
            # neighbours; the rest of the sequence was locally optimal before
            # destruction, so a cheap O(d) pass is enough to re-optimise.
            check = [False] * n
            for job in removed:
                idx = candidate.index(job)
                check[job] = True
                if idx > 0:
                    check[candidate[idx - 1]] = True
                if idx + 1 < n:
                    check[candidate[idx + 1]] = True
            candidate, cand_ms = self.local_search(candidate, loop_deadline, check, cand_ms)

            if force_accept:
                seq = candidate
                cur = cand_ms
            else:
                delta = cand_ms - cur
                if delta <= 0:
                    seq = candidate
                    cur = cand_ms
                elif not in_intens and random.random() < math.exp(-delta / temp):
                    seq = candidate
                    cur = cand_ms

            if cand_ms < best_ms:
                best = candidate[:]
                best_ms = cand_ms
                stagnation = 0
            else:
                stagnation += 1

        # Final polish: run a full insertion local search on the incumbent to
        # make it a true local optimum, then keep perturbing + re-optimising
        # while the reserved tail budget lasts.  A single full LS converges in a
        # small fraction of the tail, so most of it would otherwise be idle;
        # reinvesting it can only improve the incumbent (updates are monotone).
        if time.time() < deadline:
            best, best_ms = self.local_search(best, deadline, [True] * n, best_ms)
        while time.time() < deadline:
            dd = min(n - 1, random.randint(2, 6))
            if dd <= 0:
                break
            positions = random.sample(range(n), dd)
            pos_set = set(positions)
            remaining = [best[i] for i in range(n) if i not in pos_set]
            removed = [best[i] for i in positions]
            random.shuffle(removed)
            trial, tms = self._construct(remaining, removed)
            check = [False] * n
            for job in removed:
                idx = trial.index(job)
                check[job] = True
                if idx > 0:
                    check[trial[idx - 1]] = True
                if idx + 1 < n:
                    check[trial[idx + 1]] = True
            trial, tms = self.local_search(trial, deadline, check, tms)
            if tms < best_ms:
                best, best_ms = trial, tms
        best_ms = self.calculate_makespan(best)
        return best, best_ms, None


def plot_gantt_chart(
    sequence: List[int],
    schedules: List[List[Tuple[float, float]]],
    makespan: float,
    output_path: str,
    title: str = "PFSP Gantt Chart",
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    n_machines = len(schedules[0]) if schedules else 0
    fig_width = max(10, min(18, makespan / 80))
    fig_height = max(4, 1.0 + n_machines * 0.7)
    fig, ax = plt.subplots(figsize=(fig_width, fig_height))
    colors = plt.get_cmap("tab20", max(len(sequence), 1))

    for seq_idx, job in enumerate(sequence):
        for machine_idx, (start_time, end_time) in enumerate(schedules[seq_idx]):
            duration = end_time - start_time
            ax.barh(
                machine_idx,
                duration,
                left=start_time,
                height=0.62,
                color=colors(seq_idx % colors.N),
                edgecolor="black",
                linewidth=0.6,
            )
            if duration > makespan * 0.025:
                ax.text(
                    start_time + duration / 2,
                    machine_idx,
                    f"J{job}",
                    ha="center",
                    va="center",
                    fontsize=8,
                    color="black",
                )

    ax.set_title(f"{title} | Cmax={makespan:.0f}")
    ax.set_xlabel("Time")
    ax.set_ylabel("Machine")
    ax.set_yticks(range(n_machines))
    ax.set_yticklabels([f"M{idx}" for idx in range(n_machines)])
    ax.set_xlim(0, makespan * 1.02 if makespan > 0 else 1)
    ax.grid(axis="x", linestyle="--", linewidth=0.5, alpha=0.45)
    ax.invert_yaxis()
    fig.tight_layout()
    fig.savefig(output_path, dpi=180)
    plt.close(fig)


def main() -> None:
    # Configuration
    instance_path = "/data/lyf/problems/nwpfsp/VRF_Instances/Small/VFR20_5_1_Gap.txt"
    output_path = "/data/lyf/problems/nwpfsp/VFR20_5_1_Gap_ig_v0_pfsp_gantt.png"
    random_seed = 0

    random.seed(random_seed)
    np.random.seed(random_seed)

    processing_times, n_jobs, n_machines = read_instance(instance_path)
    time_limit = n_jobs * n_machines * 0.03
    solver = IG(processing_times, n_jobs, n_machines)
    sequence, makespan, schedules = solver.solve(time_limit=time_limit)

    plot_gantt_chart(
        sequence,
        schedules,
        makespan,
        output_path,
        title=os.path.basename(instance_path),
    )
    print(f"instance: {instance_path}")
    print(f"time_limit: {time_limit:.3f}s")
    print(f"sequence: {sequence}")
    print(f"makespan: {makespan:.0f}")
    print(f"gantt_chart: {output_path}")



if __name__ == "__main__":
    import argparse, json
    p=argparse.ArgumentParser()
    p.add_argument('--instance',required=True);p.add_argument('--output',required=True)
    p.add_argument('--time-limit',type=float,required=True);p.add_argument('--seed',type=int,required=True)
    a=p.parse_args();random.seed(a.seed);np.random.seed(a.seed % (2**32))
    matrix,n,m=read_instance(a.instance)
    sequence,makespan,_=IG(matrix,n,m).solve(time_limit=a.time_limit)
    Path(a.output).write_text(json.dumps({'permutation':[int(j) for j in sequence],'makespan':int(makespan)}))
