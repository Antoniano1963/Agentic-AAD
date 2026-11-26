"""Isolated open-source backend; no commercial solver imports."""
import os,sys,json,time,math
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'python_deps'))
begin=time.monotonic()
with open(sys.argv[1]) as f:
    n,m,t,gap,emph,threads,constant=map(float,f.readline().split());n=int(n);m=int(m)
    cols=[list(map(float,f.readline().split())) for _ in range(n)]
    rows=[]
    for _ in range(m):
        z=list(map(float,f.readline().split()));rows.append((z[0],z[1],[(int(z[i]),z[i+1]) for i in range(3,len(z),2)]))

requested_time = t
remaining = float(os.environ.get('OPEN_MILP_DEADLINE',str(time.time()+3600)))-time.time()
t = max(.01,min(t if math.isfinite(t) else 3600.,max(.01,remaining),3600.))

backend=os.environ.get('OPEN_MILP_BACKEND','scip')
if backend.startswith('scip'):
    from pyscipopt import Model,quicksum,SCIP_PARAMEMPHASIS
    s=Model();s.hideOutput();s.setRealParam('limits/time',max(.01,t));s.setRealParam('limits/gap',max(0,gap))
    # Sequential SCIP keeps the full solve inside the task's CPU allowance.
    s.setIntParam('parallel/maxnthreads',1)
    if backend=='scip_feas':s.setEmphasis(SCIP_PARAMEMPHASIS.FEASIBILITY)
    v=[s.addVar(name='v'+str(i),lb=c[0],ub=c[1],vtype=('C' if c[2]==0 else 'B' if c[2]==2 else 'I'),obj=c[3]) for i,c in enumerate(cols)]
    for lo,hi,a in rows:
        e=quicksum(co*v[i] for i,co in a)
        if lo==hi:s.addCons(e==lo)
        else:
            if lo>-1e19:s.addCons(e>=lo)
            if hi<1e19:s.addCons(e<=hi)
    warm=[(i,c[4]) for i,c in enumerate(cols) if abs(c[4])<1e90 and c[0]-1e-7<=c[4]<=c[1]+1e-7]
    if warm:
        sol=s.createPartialSol()
        for i,x in warm:s.setSolVal(sol,v[i],x)
        s.addSol(sol)
    s.optimize();status=str(s.getStatus());ok=s.getNSols()>0;optimal=status=='optimal'
    values=[s.getSolVal(s.getBestSol(),x) for x in v] if ok else []
    bound=float(s.getDualbound())+constant
elif backend=='highs':
    import highspy,numpy as np
    s=highspy.Highs();s.setOptionValue('output_flag',False);s.setOptionValue('time_limit',max(.01,t));s.setOptionValue('mip_rel_gap',max(0,gap));s.setOptionValue('threads',min(8,int(threads)))
    s.addVars(n,np.array([c[0] for c in cols]),np.array([c[1] for c in cols]))
    s.changeColsCost(n,np.arange(n,dtype=np.int32),np.array([c[3] for c in cols]))
    ints=[i for i,c in enumerate(cols) if c[2]!=0]
    if ints:s.changeColsIntegrality(len(ints),np.array(ints,dtype=np.int32),np.ones(len(ints),dtype=np.uint8))
    for lo,hi,a in rows:s.addRow(lo,hi,len(a),np.array([i for i,c in a],dtype=np.int32),np.array([c for i,c in a],dtype=float))
    warm=[(i,c[4]) for i,c in enumerate(cols) if abs(c[4])<1e90 and c[0]-1e-7<=c[4]<=c[1]+1e-7]
    if warm:s.setSolution(len(warm),np.array([i for i,x in warm],dtype=np.int32),np.array([x for i,x in warm]))
    s.run();info=s.getInfo();status=str(s.getModelStatus());ok=info.primal_solution_status==2;optimal=s.getModelStatus()==highspy.HighsModelStatus.kOptimal
    values=s.getSolution().col_value if ok else [];bound=float(info.mip_dual_bound)+constant
    if not ints and ok:bound=sum(c[3]*x for c,x in zip(cols,values))+constant
else:raise ValueError(backend)
proven_infeasible = status in ('infeasible', 'HighsModelStatus.kInfeasible')
obj=sum(c[3]*x for c,x in zip(cols,values))+constant if ok else 1e20
violation=0.
if ok:
    for c,x in zip(cols,values):
        violation=max(violation,c[0]-x,x-c[1])
        if c[2]!=0:violation=max(violation,abs(x-round(x)))
    for lo,hi,a in rows:
        val=sum(co*values[i] for i,co in a);scale=max(1,abs(val),abs(lo) if abs(lo)<1e19 else 0,abs(hi) if abs(hi)<1e19 else 0)
        violation=max(violation,max(lo-val,val-hi)/scale)
    if violation>1e-5:raise RuntimeError(f'Backend returned unacceptable primal violation {violation}')
if not math.isfinite(bound):bound=-1e20
with open(sys.argv[2],'w') as f:
    f.write(f'{int(ok)} {int(optimal)} {obj:.17g} {bound:.17g} {int(proven_infeasible)}\n');f.write(' '.join(f'{x:.17g}' for x in values))
with open('milp_backend.jsonl','a') as f:f.write(json.dumps(dict(backend=backend,n=n,m=m,budget=t,status=status,feasible=bool(ok),objective=obj,bound=bound,violation=violation,elapsed=time.monotonic()-begin))+'\n')
