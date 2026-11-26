import argparse,contextlib,importlib.util,io,json,math,sys,traceback
from pathlib import Path

root=Path(__file__).resolve().parent
sys.path.insert(0,str(root/'python_deps'))
p=argparse.ArgumentParser();p.add_argument('--instance',required=True);p.add_argument('--solution',required=True);p.add_argument('--output-dir',required=True);a=p.parse_args()
out=Path(a.output_dir);out.mkdir(parents=True,exist_ok=True)
log=io.StringIO();result={'valid':False,'objective':None,'instance':a.instance,'solution':a.solution}
try:
 spec=importlib.util.spec_from_file_location('official_checker',root/'RTE_ChallengeROADEF2020_checker.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 with contextlib.redirect_stdout(log):
  data=m.read_json(a.instance)
  entries=[s.split() for s in Path(a.solution).read_text().splitlines() if s.strip()]
  names=[s[0] for s in entries if len(s)==2]
  shape=len(names)==len(entries)==len(data[m.INTERVENTIONS_STR]) and len(set(names))==len(names) and set(names)==set(data[m.INTERVENTIONS_STR])
  if not shape:raise ValueError('Missing, duplicate, unknown intervention or malformed solution line')
  m.read_solution_from_txt(data,a.solution);m.check_all_constraints(data)
  mean,q=m.compute_objective(data);m.display_basic(data,mean,q)
  import numpy as np
  obj=float(data[m.ALPHA_STR]*np.mean(mean)+(1-data[m.ALPHA_STR])*np.mean(np.maximum(np.asarray(q)-np.asarray(mean),0)))
  result['valid']='ERROR' not in log.getvalue() and math.isfinite(obj)
  if result['valid']:result['objective']=obj
except Exception as e:
 result['error']=repr(e);log.write(traceback.format_exc())
(out/'checker.log').write_text(log.getvalue());(out/'check.json').write_text(json.dumps(result,indent=2));print(json.dumps(result));sys.exit(0 if result['valid'] else 1)
