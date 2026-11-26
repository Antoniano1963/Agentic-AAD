"""Run the original iterated greedy solver on one VFR instance."""
import argparse
import json
import random
import time
from pathlib import Path

import numpy as np
from check_solution import read_instance, validate
from ig_pfsp import IG


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--instance', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--time-limit', type=float, required=True)
    parser.add_argument('--seed', type=int, required=True)
    args = parser.parse_args()
    if args.time_limit <= 0:
        parser.error('The time limit must be positive.')
    matrix = np.asarray(read_instance(args.instance))
    random.seed(args.seed)
    np.random.seed(args.seed % 2**32)
    sequence, cost, _ = IG(matrix, *matrix.shape).solve(time.time(), args.time_limit)
    result = {'permutation': list(map(int, sequence)), 'makespan': int(cost)}
    args.output.write_text(json.dumps(result) + '\n')
    verdict = validate(args.instance, args.output)
    if not verdict['valid']:
        raise ValueError(verdict)


if __name__ == '__main__':
    main()
