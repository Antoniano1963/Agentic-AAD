"""One isolated subprocess per solve; no API key or evaluation UB is supplied."""
import sys,json,time,random,math,resource
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from pfsp_adapter.frozen_ig import IG
from pfsp_adapter.operator_loader import load_operator,validate_partition

def main():
    payload=json.load(sys.stdin);limit=payload['time_limit']
    resource.setrlimit(resource.RLIMIT_CPU,(math.ceil(limit)+20,math.ceil(limit)+21))
    resource.setrlimit(resource.RLIMIT_AS,(3*1024**3,3*1024**3))
    resource.setrlimit(resource.RLIMIT_FSIZE,(16*1024**2,16*1024**2))
    source=Path(payload['code']).read_text()
    operator=load_operator(source)
    random.seed(payload['seed']);np.random.seed(payload['seed']%(2**32))
    matrix=payload['matrix'];before=json.dumps(matrix)
    class FixedIG(IG):
        def destruction_phase(self,sequence):
            return validate_partition(sequence,operator(list(sequence),matrix))
    n=len(matrix);m=len(matrix[0]);start=time.time();cpu=time.process_time()
    solver=FixedIG(np.array(matrix,dtype=int),n,m)
    seq,cost,_=solver.solve(start_time=start,time_limit=limit)
    if json.dumps(matrix)!=before:raise ValueError('Operator modified processing_times')
    result=dict(sequence=[int(x) for x in seq],makespan=int(cost),elapsed_seconds=time.time()-start,cpu_seconds=time.process_time()-cpu)
    print(json.dumps(result))

if __name__=='__main__':main()
