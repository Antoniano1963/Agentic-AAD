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
        self.n_jobs = n_jobs
        self.n_machines = n_machines
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

    def calculate_makespan(self, sequence: List[int]) -> float:
        """计算makespan"""
        n_jobs = len(sequence)
        n_machines = self.n_machines

        # 初始化评估矩阵
        e = np.zeros((n_jobs+1, n_machines+1))

        # 计算e值（最早完成时间）
        for i in range(1, n_jobs+1):
            for j in range(1, n_machines+1):
                e[i, j] = max(e[i, j-1], e[i-1, j]) + self.processing_times[sequence[i-1]][j-1]

        makespan = e[n_jobs, n_machines]
        return makespan

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

    def solve(self, start_time=None, time_limit=None, instance_name="unknown") -> Tuple[List[int], float, List[List[Tuple[float, float]]]]:
        """
        求解 PFSP 问题
        """
        start_time = time.time() if start_time is None else start_time
        if time_limit is None:
            time_limit = self.n_jobs * self.n_machines * 0.03
        actual_time_limit = time_limit * 0.99
        temperature_factor = 0.4
        total_processing_time = float(self.processing_times.sum())
        temperature = temperature_factor * (
            total_processing_time / (self.n_jobs * self.n_machines * 10.0)
        )
        current_sequence, current_makespan = self.neh_heuristic()
        current_sequence = self.local_search_insertion(current_sequence, start_time, actual_time_limit)
        current_sequence = validate_solution_sequence(
            current_sequence, self.n_jobs, context="PFSP initial solution"
        )
        current_makespan = self.calculate_makespan(current_sequence)
        best_sequence = current_sequence.copy()
        best_makespan = current_makespan
        iteration = 0
        while True:
            if actual_time_limit is not None:
                current_time = time.time()
                if (current_time - start_time) >= actual_time_limit:
                    break
            #破坏
            current_sequence = validate_solution_sequence(
                current_sequence, self.n_jobs, context="PFSP pre-destroy solution"
            )
            destroy_result = self.destruction_phase(current_sequence)
            remaining_sequence, removed_jobs = validate_destroy_result(
                current_sequence, destroy_result, context="PFSP destruction_phase"
            )
            #构造，传入破坏前的 current_sequence 作为 fallback
            new_sequence = self.construction_phase(remaining_sequence, removed_jobs, start_time, actual_time_limit, fallback_sequence=current_sequence)
            new_sequence = validate_solution_sequence(
                new_sequence, self.n_jobs, context="PFSP reconstructed solution"
            )
            #局部搜索
            new_sequence = self.local_search_insertion(new_sequence, start_time, actual_time_limit)
            new_sequence = validate_solution_sequence(
                new_sequence, self.n_jobs, context="PFSP local-search solution"
            )
            makespan = self.calculate_makespan(new_sequence)
            delta = makespan - current_makespan
            improved = makespan < best_makespan
            if delta <= 0:
                current_sequence = new_sequence.copy()
                current_makespan = makespan
                if improved:
                    best_sequence = new_sequence.copy()
                    best_makespan = makespan
            else:
                acceptance_probability = math.exp(-delta / temperature)
                if random.random() < acceptance_probability:
                    current_sequence = new_sequence.copy()
                    current_makespan = makespan
            iteration += 1
        best_sequence = validate_solution_sequence(
            best_sequence, self.n_jobs, context="PFSP final solution"
        )
        _, best_schedules = self.calculate_objectives(best_sequence)
        return best_sequence, best_makespan, best_schedules


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
    main()
