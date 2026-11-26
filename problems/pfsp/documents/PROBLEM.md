# PFSP Data and Output Specification

> **Translation note:** This file is an English translation of the original Chinese problem specification provided during the experiments.

`n` jobs are processed sequentially through `m` machines. All machines use the same job permutation; each machine can process at most one job at a time, and each job can be processed on at most one machine at a time; operations are non-preemptive. Find a permutation covering all jobs that minimizes the completion time of the last job on the last machine (makespan).

The first line of the input text is `n m`, followed by `n` lines describing jobs `0` through `n-1` in order. Each line contains `m` pairs of integers `machine_id processing_time`, where machine IDs range from `0` to `m-1`. Machines process jobs in ascending order of machine ID.

Given a permutation `pi`, the completion time recurrence is `C[i,k] = max(C[i-1,k], C[i,k-1]) + p[pi[i],k]`, with boundary completion times set to 0. The objective is `C[n-1,m-1]`.

The output JSON must contain an integer array `permutation` of length `n`, where each integer from `0` to `n-1` appears exactly once. The optional field `makespan` is verified by an independent checker. Verification command:
```bash
python3 tools/check_solution.py --instance training/VFR100_20_1_Gap.txt --solution path/to/solution.json
```
The training reference value BKR is a fixed pre-existing reference upper bound; it is not a reference solution provided to you, nor is it guaranteed to be a proven optimum.
