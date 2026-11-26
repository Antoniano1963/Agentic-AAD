"""Transport for held-out EAX binaries. The frozen source determines the restart policy."""
import argparse, json, os, subprocess, sys, tempfile
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('candidate',type=Path)
    p.add_argument('--instance',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--time-limit',required=True,type=float)
    p.add_argument('--seed',required=True,type=int)
    p.add_argument('--max-restarts',type=int,choices=[10],default=10)
    p.add_argument('--metadata',type=Path)
    a=p.parse_args()
    c=a.candidate.resolve()
    inst=a.instance.resolve()
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(root/'checker'))
    from check_tour import validate
    refs=json.loads((root/'runtime/reference_lengths.json').read_text())
    with inst.open() as f:
        n=next(int(line.split(':')[-1].strip()) for line in f if line.strip().startswith('DIMENSION'))
    pop={'eax100':100,'eax300':300,'kimi':100 if n>14000 else 200,'deepseek':100,'glm':300,'gpt':100}[c.name]
    env=dict(os.environ,EAX_SEED=str(a.seed),EAX_TOTAL_TIME_LIMIT=str(a.time_limit),EAX_LOG_INTERVAL='1',EAX_MAX_RESTARTS='10',OMP_NUM_THREADS='1')
    if inst.stem in refs:env['EAX_BKS']=str(refs[inst.stem])
    with tempfile.TemporaryDirectory() as tmp:
        d=Path(tmp)
        subprocess.run([str(c/'jikken'),'11' if c.name=='glm' else '1','eax',str(pop),'30',str(inst)],cwd=d,env=env,check=True)
        lines=(d/'eax_BestSol').read_text().splitlines()
        size,length=map(int,lines[0].split())
        tour=list(map(int,lines[1].split()))
        if len(tour)!=size:raise ValueError('Tour size mismatch')
        a.output.write_text('TYPE: TOUR\nDIMENSION: '+str(size)+'\nTOUR_SECTION\n'+'\n'.join(map(str,tour))+'\n-1\nEOF\n')
        verdict=validate(inst,a.output)
        if not verdict['valid'] or verdict['length']!=length:raise ValueError(verdict)
        if a.metadata:
            data=json.loads((d/'eax_total.json').read_text())
            data.update(reported_length=length,population=pop,offspring=30)
            a.metadata.write_text(json.dumps(data)+'\n')
if __name__=='__main__':main()

