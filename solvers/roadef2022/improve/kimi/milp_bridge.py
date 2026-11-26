"""Isolated open-source backend; no commercial solver imports.

v002 changes vs baseline (algorithm-neutral infrastructure):
- Fast binary parse and batched CSR model build for HiGHS (single addRows call).
- Per-worker model cache: identical model data (ignoring time/gap header and
  warm-start column) reuses the Highs instance, avoiding a full rebuild on the
  retry loop, which only changes time_limit / mip_rel_gap.
- Telemetry adds cache hit, node count and parse timing.
"""
import os,sys,json,time,math,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'python_deps'))
begin=time.monotonic()
with open(sys.argv[1],'rb') as f:
    raw=f.read()
lines=raw.split(b'\n')
n,m,t,gap,emph,threads,constant=map(float,lines[0].split());n=int(n);m=int(m)
import numpy as np
lb=np.empty(n);ub=np.empty(n);typ=np.empty(n);cost=np.empty(n);warmv=np.empty(n)
for i in range(n):
    a,b2,c,d,e=lines[1+i].split()
    lb[i]=a;ub[i]=b2;typ[i]=c;cost[i]=d;warmv[i]=e
rlo=np.empty(m);rhi=np.empty(m)
starts=np.zeros(m,dtype=np.int64)
idx_parts=[];val_parts=[];nnz=0
pos=1+n
for j in range(m):
    z=lines[pos+j].split()
    rlo[j]=z[0];rhi[j]=z[1]
    starts[j]=nnz
    for i2 in range(3,len(z),2):
        idx_parts.append(int(z[i2]));val_parts.append(float(z[i2+1]))
        nnz+=1
ridx=np.array(idx_parts,dtype=np.int64);rval=np.array(val_parts,dtype=np.float64)
parse_done=time.monotonic()

requested_time = t
remaining = float(os.environ.get('OPEN_MILP_DEADLINE',str(time.time()+3600)))-time.time()
t = max(.01,min(t if math.isfinite(t) else 3600.,max(.01,remaining),3600.))

backend=os.environ.get('OPEN_MILP_BACKEND','scip')
cache_hit=False
node_count=-1
if backend.startswith('scip'):
    from pyscipopt import Model,quicksum,SCIP_PARAMEMPHASIS
    s=Model();s.hideOutput();s.setRealParam('limits/time',max(.01,t));s.setRealParam('limits/gap',max(0,gap))
    # Sequential SCIP keeps the full solve inside the task's CPU allowance.
    s.setIntParam('parallel/maxnthreads',1)
    if backend=='scip_feas':s.setEmphasis(SCIP_PARAMEMPHASIS.FEASIBILITY)
    v=[s.addVar(name='v'+str(i),lb=float(lb[i]),ub=float(ub[i]),vtype=('C' if typ[i]==0 else 'B' if typ[i]==2 else 'I'),obj=float(cost[i])) for i in range(n)]
    pos=1+n
    for j in range(m):
        z=lines[pos+j].split()
        e=quicksum(float(z[i2+1])*v[int(z[i2])] for i2 in range(3,len(z),2))
        lo,hi=float(rlo[j]),float(rhi[j])
        if lo==hi:s.addCons(e==lo)
        else:
            if lo>-1e19:s.addCons(e>=lo)
            if hi<1e19:s.addCons(e<=hi)
    warm=[(i,float(warmv[i])) for i in range(n) if abs(warmv[i])<1e90 and lb[i]-1e-7<=warmv[i]<=ub[i]+1e-7]
    if warm:
        sol=s.createPartialSol()
        for i,x in warm:s.setSolVal(sol,v[i],x)
        s.addSol(sol)
    s.optimize();status=str(s.getStatus());ok=s.getNSols()>0;optimal=status=='optimal'
    values=np.array([s.getSolVal(s.getBestSol(),x) for x in v]) if ok else np.zeros(0)
    bound=float(s.getDualbound())+constant
elif backend=='highs':
    import highspy
    h=hashlib.sha1()
    h.update(str(n).encode());h.update(str(m).encode());h.update(repr(constant).encode())
    for arr in (lb,ub,typ,cost,rlo,rhi,ridx,rval):
        h.update(np.ascontiguousarray(arr).tobytes())
    key=h.digest()
    cache=getattr(sys,'_milp_cache',None)
    if cache is None:
        cache={};sys._milp_cache=cache
    if cache.get('key')==key:
        s=cache['highs'];cache_hit=True
        s.clearSolver()
        s.setOptionValue('time_limit',max(.01,t));s.setOptionValue('mip_rel_gap',max(0,gap))
    else:
        s=highspy.Highs();s.setOptionValue('output_flag',False);s.setOptionValue('time_limit',max(.01,t));s.setOptionValue('mip_rel_gap',max(0,gap));s.setOptionValue('threads',min(8,int(threads)))
        s.addVars(n,lb,ub)
        s.changeColsCost(n,np.arange(n,dtype=np.int32),cost)
        ints=np.nonzero(typ!=0)[0].astype(np.int32)
        if len(ints):s.changeColsIntegrality(len(ints),ints,np.ones(len(ints),dtype=np.uint8))
        s.addRows(m,rlo,rhi,nnz,starts.astype(np.int32),ridx.astype(np.int32),rval)
        cache.clear();cache['key']=key;cache['highs']=s
    wmask=(np.abs(warmv)<1e90)&(warmv>=lb-1e-7)&(warmv<=ub+1e-7)
    wi=np.nonzero(wmask)[0].astype(np.int32)
    if len(wi):s.setSolution(len(wi),wi,warmv[wi])
    s.run();info=s.getInfo();status=str(s.getModelStatus());ok=info.primal_solution_status==2;optimal=s.getModelStatus()==highspy.HighsModelStatus.kOptimal
    values=np.asarray(s.getSolution().col_value) if ok else np.zeros(0)
    bound=float(info.mip_dual_bound)+constant
    if not np.any(typ!=0) and ok:bound=float(np.dot(cost,values))+constant
    node_count=int(getattr(info,'mip_node_count',-1))
else:raise ValueError(backend)
proven_infeasible = status in ('infeasible', 'HighsModelStatus.kInfeasible')
obj=float(np.dot(cost,values))+constant if ok else 1e20
violation=0.
if ok:
    violation=max(0.,float((lb-values).max(initial=0.)),float((values-ub).max(initial=0.)))
    imask=typ!=0
    if np.any(imask):
        violation=max(violation,float(np.abs(values[imask]-np.round(values[imask])).max(initial=0.)))
    if m>0:
        lens=np.diff(np.append(starts,nnz))
        if nnz>0:
            contrib=rval*values[ridx]
            starts_c=np.minimum(starts,nnz-1)
            rowsum=np.add.reduceat(contrib,starts_c)
            rowsum=np.where(lens>0,rowsum,0.)
        else:
            rowsum=np.zeros(m)
        scale=np.maximum.reduce([np.ones(m),np.abs(rowsum),
                                 np.where(np.abs(rlo)<1e19,np.abs(rlo),0.),
                                 np.where(np.abs(rhi)<1e19,np.abs(rhi),0.)])
        violation=max(violation,float((np.maximum(rlo-rowsum,rowsum-rhi)/scale).max(initial=0.)))
    if violation>1e-5:raise RuntimeError(f'Backend returned unacceptable primal violation {violation}')
if not math.isfinite(bound):bound=-1e20
with open(sys.argv[2],'w') as f:
    f.write(f'{int(ok)} {int(optimal)} {obj:.17g} {bound:.17g} {int(proven_infeasible)}\n');f.write(' '.join(f'{x:.17g}' for x in values))
with open('milp_backend.jsonl','a') as f:f.write(json.dumps(dict(backend=backend,n=n,m=m,budget=t,status=status,feasible=bool(ok),objective=obj,bound=bound,violation=violation,cache_hit=cache_hit,nodes=node_count,parse_seconds=parse_done-begin,elapsed=time.monotonic()-begin))+'\n')
