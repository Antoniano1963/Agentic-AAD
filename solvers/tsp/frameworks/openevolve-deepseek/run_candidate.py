"""Fixed transport wrapper; does not change EAX search code."""
from pathlib import Path
import argparse,os,subprocess,json,time
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True);p.add_argument('--instance',required=True);p.add_argument('--output',required=True);p.add_argument('--time-limit',type=float,required=True);p.add_argument('--seed',type=int,required=True);p.add_argument('--max-restarts',type=int,required=True);p.add_argument('--metadata',required=True);a=p.parse_args()
assert a.max_restarts==10
c=Path(a.candidate);params=json.loads((c/'parameters.json').read_text());env={k:v for k,v in os.environ.items() if not k.startswith('EAX_') and 'API_KEY' not in k}
env.update(EAX_SEED=str(a.seed),EAX_TOTAL_TIME_LIMIT=str(a.time_limit),EAX_MAX_RESTARTS='10',EAX_LOG_INTERVAL='1',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
started=time.monotonic()
rc=subprocess.call([str(c/'jikken'),'1','eax',str(params['population']),str(params['offspring']),a.instance],env=env)
if rc:raise SystemExit(rc)
lines=Path('eax_BestSol').read_text().splitlines();n,length=map(int,lines[0].split());tour=list(map(int,lines[1].split()));assert n==len(tour)
Path(a.output).write_text('NAME: candidate\nTYPE: TOUR\nDIMENSION: '+str(n)+'\nTOUR_SECTION\n'+'\n'.join(map(str,tour))+'\n-1\nEOF\n')
meta=json.loads(Path('eax_total.json').read_text());meta.update(reported_length=length,wrapper_wall_seconds=time.monotonic()-started)
Path(a.metadata).write_text(json.dumps(meta))
