"""CLI for the historical Python PFSP solvers. All job IDs are zero based."""
import argparse, importlib.util, json, os, random, subprocess, sys, time
from pathlib import Path
import numpy as np

def main():
    p = argparse.ArgumentParser()
    p.add_argument('candidate', type=Path)
    p.add_argument('--instance', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--seed', required=True, type=int)
    p.add_argument('--time-limit', required=True, type=float)
    a = p.parse_args()
    d = a.candidate.resolve()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'checker'))
    from check_solution import read_instance, validate
    matrix = read_instance(a.instance)
    random.seed(a.seed)
    np.random.seed(a.seed % 2**32)
    reported = None
    if d.name == 'reevo-deepseek':
        req = dict(matrix=matrix, seed=a.seed, time_limit=a.time_limit, code=str(d/'best_operator.py'))
        raw = subprocess.check_output([sys.executable, str(d/'pfsp_adapter/worker.py')], input=json.dumps(req), text=True)
        result = json.loads(raw.strip().splitlines()[-1])
        seq = result['sequence']
        reported = result['makespan']
    else:
        sys.path.insert(0, str(d))
        fn = {'ig':'ig_pfsp.py','alns':'basic_alns.py'}.get(d.name)
        file = d/fn if fn else next(d.glob('v*.py'))
        spec = importlib.util.spec_from_file_location('candidate', file)
        solver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(solver)
        if d.name == 'ig':
            seq, reported, _ = solver.IG(np.asarray(matrix), len(matrix), len(matrix[0])).solve(time.time(), a.time_limit)
        else:
            result = solver.solve(np.asarray(matrix) if d.name == 'alns' else matrix,a.seed,a.time_limit)
            seq = result[0] if isinstance(result,tuple) else result
            if isinstance(result,tuple): reported = result[1]
    payload = {'permutation':list(map(int,seq))}
    if reported is not None: payload['makespan'] = float(reported)
    a.output.write_text(json.dumps(payload))
    verdict = validate(a.instance,a.output)
    if not verdict['valid']:
        raise ValueError(verdict)
    data=json.loads(a.output.read_text())
    data['makespan']=verdict['makespan']
    a.output.write_text(json.dumps(data)+'\n')
if __name__ == '__main__':
    main()

