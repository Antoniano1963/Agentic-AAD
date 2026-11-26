"""Run a solver on one benchmark instance and check the solution. Build the candidate first. Use a new output directory."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def invoke(args, directory, log, env, check=True, before_exec=None):
    with log.open('wb') as f:
        return subprocess.run(list(map(str,args)), cwd=directory, env=env,
                              stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT,
                              check=check, preexec_fn=before_exec)

def limit_glass_memory():
    import resource
    soft, hard = resource.getrlimit(resource.RLIMIT_AS)
    limit = min(value for value in (48_000_000_000, soft, hard) if value != resource.RLIM_INFINITY)
    resource.setrlimit(resource.RLIMIT_AS, (limit, hard))

def solver_entry(domain, mode, name, solver, problem):
    python = sys.executable
    if domain == 'pfsp':
        if mode == 'baseline':
            return [python, solver/'run_ig.py']
        if mode == 'from_scratch':
            return [solver/('solver' if name == 'deepseek' else 'pfsp_solver')]
        if name == 'openevolve-deepseek':
            return [python, solver/'solver.py']
        return [python, problem/'runtime/run_python.py', solver]
    if domain == 'tsp':
        if mode == 'baseline':
            return [python, solver/'run_eax.py']
        if mode == 'improve':
            return [python, problem/'runtime/run_eax.py', solver]
        if mode == 'frameworks':
            return [python, solver/'run_candidate.py', '--candidate', solver]
        binaries = {'deepseek':'bin/tsp_solver', 'glm':'solver', 'kimi':'tsp_solver'}
        return [solver/binaries[name]]
    if domain == 'roadef2022':
        if mode != 'from_scratch':
            return [solver/'s46_open']
        if name == 'kimi':
            return [python, solver/'solver.py']
    return [solver/'solver']

def glass_portfolio(solver, inst, name):
    if name == 'glm':
        portfolio = json.loads((solver/'portfolio.json').read_text())
        with Path(str(inst)+'_batch.csv').open(newline='') as f:
            stacks = {row['STACK'] for row in csv.DictReader(f, delimiter=';')}
        key = 'many_stacks' if len(stacks) >= portfolio['stack_threshold'] else 'few_stacks'
        return ['-a', *portfolio[key]]
    if name == 'deepseek':
        return ['-a', *json.loads((solver/'final_algorithms.json').read_text())]
    return []

def verify_2018(d, problem, inst, sol, env):
    data = d/'instances_checker'
    data.mkdir()
    (d/'logs').mkdir()
    for part in ('batch','defects'):
        path = Path(str(inst)+'_'+part+'.csv')
        (data/path.name).symlink_to(path)
    (data/'global_param.csv').symlink_to(problem/'common/global_param.csv')
    (data/(inst.name+'_solution.csv')).symlink_to(sol)
    check = d/'checker'
    shutil.copyfile(problem/'checker/bin/checker', check)
    check.chmod(0o755)
    invoke([check, inst.name, '0'],d,d/'checker.log',env)
    rows = [r for f in (d/'logs').glob('*statistics.csv') for r in csv.DictReader(f.open(),delimiter=';')]
    return dict(valid=len(rows)==1 and rows[0].get('validSolution')=='1', statistics=rows)

def verify_2022(d, problem, env):
    names = 'input_items input_trucks input_parameters output_items output_stacks output_trucks checker_report'.split()
    header = 'input items pathFilename;input trucks pathFilename;input parameters pathFilename;output items pathFilename;output stacks pathFilename;output trucks pathFilename;report pathFilename'
    job = d/'instances.csv'
    job.write_text(header+'\n'+';'.join(str(d/(n+'.csv')) for n in names)+'\n')
    invoke(['java','-Xmx4g','-XX:ActiveProcessorCount=2','-Duser.language=en','-Duser.country=US',
            '-DLOG_FILENAME='+str(d/'checker_log'),'-jar',problem/'checker/CheckerChallenge-1.11.4.jar',job],d,d/'checker.log',env)
    report=(d/'checker_report.csv').read_text(encoding='latin1')
    cost=float(report.splitlines()[1].split(';')[2].replace(',','.')) if report.startswith('transportation cost;') else None
    valid=cost is not None and math.isfinite(cost) and cost!=999999999 and 'Constraint ' not in report and 'Crash !!' not in (d/'checker.log').read_text(errors='replace')
    return dict(valid=valid,objective=cost if valid else None)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    choice=p.add_mutually_exclusive_group(required=True)
    choice.add_argument('--solver',help='Selected candidate, e.g. pfsp/from_scratch/kimi')
    choice.add_argument('--baseline',choices=('pfsp','tsp','roadef2018','roadef2020','roadef2022'),help='Base algorithm for one domain')
    p.add_argument('--split',choices=('train','test','example'),default='train',help='ROADEF 2020 also provides two small official examples.')
    p.add_argument('--instance',required=True)
    p.add_argument('--seconds',type=int,required=True,help='Solver time; this command is a single-run functional check.')
    p.add_argument('--seed',type=int,default=1111)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if a.baseline:
        domain,mode,name=a.baseline,'baseline',None
        solver=ROOT/'baselines'/domain
    else:
        base=ROOT/'solvers'
        solver=(base/a.solver).resolve()
        domain,mode,name=solver.relative_to(base).parts
    if a.seconds<=0 or not (solver/'build.sh').is_file():p.error('Expected a known solver and positive time limit')
    problem=ROOT/'problems'/domain
    entry=solver_entry(domain,mode,name,solver,problem)
    if not Path(entry[0]).is_file():p.error(f'Build the solver first with bash "{solver / "build.sh"}"')
    s46 = domain == 'roadef2022' and mode != 'from_scratch'
    if a.split=='example':
        if domain!='roadef2020' or a.instance not in ('example1','example2'):
            p.error('Use example1 or example2 with ROADEF 2020.')
        inst=problem/'checker'/(a.instance+'.json')
    else:
        with (problem/'instances.csv').open(encoding='utf-8-sig', newline='') as stream:
            row=next((r for r in csv.DictReader(stream) if r['instance']==a.instance and r['split']==a.split),None)
        if row is None:p.error('Instance not in this split')
        inst=problem/a.split/row['file']
    d=a.out.resolve();d.mkdir(parents=True,exist_ok=False)
    sol=d/'solution'
    env=dict(os.environ,OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
    env['OMP_NUM_THREADS']='4' if domain=='roadef2020' and mode in ('improve','baseline') else '8' if domain=='roadef2022' else '1'
    t=a.seconds;s=a.seed
    if domain in ('pfsp','tsp'):
        args=['--instance',inst,'--output',sol,'--time-limit',t,'--seed',s]
        if domain=='tsp':args+=['--max-restarts',10,'--metadata',d/'metadata.json']
    elif domain=='roadef2018':
        params=problem/'common/global_param.csv'
        args=(['--instance',inst,'--params',params,'--output',sol,'--time-limit',t,'--seed',s] if mode=='from_scratch'
              else ['-p',inst,'-g',params,'-o',sol,'-i',d/'info.ini','-t',t,'-s',s])
        if mode == 'improve':
            args = glass_portfolio(solver, inst, name) + args
    elif domain=='roadef2020':
        if a.split!='example':
            member=row['archive_member']
            tool=shutil.which('7zz') or shutil.which('7z')
            invoke([tool,'x',inst,member,'-o'+str(d),'-y'] if tool else ['bsdtar','-xf',inst,member],d,d/'extract.log',env)
            inst=d/member
        if mode!='from_scratch':args=['-p',inst,'-o',sol,'-t',t,'-s',s]
        elif name=='glm':args=['--instance',inst,'--solution',sol,'--time-budget',t,'--threads',4,'--samples',224,'--seed',s]
        elif name=='kimi':args=['--instance',inst,'--output',sol,'--time',t,'--seed',s]
        else:args=['--instance',inst,'--out',sol,'--time',t,'--seed',s,'--cycle','1.0']
    else:
        with zipfile.ZipFile(inst) as z:
            for n in ('input_items.csv','input_parameters.csv','input_trucks.csv'):
                (d/n).write_bytes(z.read(row['archive_member']+'/'+n))
        params=d/'input_parameters.csv';lines=params.read_text().splitlines()
        fields=lines[1].split(';');fields[-1]=str(t);lines[1]=';'.join(fields);params.write_text('\n'.join(lines)+'\n')
        if s46:
            env.update(OPEN_MILP_BRIDGE=str(solver/'milp_bridge.py'),OPEN_MILP_BACKEND='highs')
            if name == 'deepseek':
                env.update(S46_SAME_DAY_MERGE='1',S46_SAME_DAY_CAP='30',S46_PROGRESS='1')
            args=['-InputFile',str(d)+'/', '-OutputFile',str(d)+'/', '-Tipo','17','-NThreads','8','-Pruebas','0']
        else:
            args=[d,d,t,s]
    cmd=[*entry,*args]
    (d/'command.json').write_text(json.dumps(list(map(str,cmd)),indent=2))
    limiter = limit_glass_memory if (domain,mode,name) == ('roadef2018','from_scratch','kimi') else None
    result = invoke(cmd,d,d/'solver.log',env,check=not s46,before_exec=limiter)
    if s46:
        if result.returncode == 1:
            for part in ('items','stacks','trucks'):
                shutil.copyfile(d/f'output_{part}17.csv', d/f'output_{part}.csv')
        else:
            result.check_returncode()
    if domain in ('pfsp','tsp'):
        tsp=domain=='tsp'
        invoke([sys.executable,problem/'checker'/('check_tour.py' if tsp else 'check_solution.py'),
                '--instance',inst,'--tour' if tsp else '--solution',sol],d,d/'checker.log',env,check=False)
        verdict=json.loads((d/'checker.log').read_text())
    elif domain=='roadef2018':verdict=verify_2018(d,problem,inst,sol,env)
    elif domain=='roadef2020':
        invoke([sys.executable,problem/'checker/check.py','--instance',inst,'--solution',sol,'--output-dir',d/'check'],d,d/'checker.log',env,check=False)
        verdict=json.loads((d/'check/check.json').read_text())
    else:verdict=verify_2022(d,problem,env)
    (d/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
    print(json.dumps(verdict));return 0 if verdict['valid'] else 1

if __name__=='__main__':sys.exit(main())

