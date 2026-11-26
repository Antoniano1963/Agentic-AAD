"""PFSP permutation and makespan checker. Contains no search algorithm."""
from pathlib import Path
import argparse,json

def read_instance(path):
 lines=[x for x in Path(path).read_text().splitlines() if x.strip()]
 n,m=map(int,lines[0].split());assert len(lines)==n+1
 matrix=[]
 for line in lines[1:]:
  values=list(map(int,line.split()));assert len(values)==2*m
  ids=values[::2];assert sorted(ids)==list(range(m))
  row=[0]*m
  for k,v in zip(ids,values[1::2]):assert v>=0;row[k]=v
  matrix.append(row)
 return matrix

def validate(instance,solution):
 try:
  matrix=read_instance(instance);n=len(matrix);m=len(matrix[0]);d=json.loads(Path(solution).read_text());seq=d['permutation']
  assert isinstance(seq,list) and len(seq)==n and all(type(x)==int for x in seq) and sorted(seq)==list(range(n)), 'Expected a permutation of 0..n-1'
  finish=[0]*m
  for j in seq:
   last=0
   for k in range(m):last=max(last,finish[k])+matrix[j][k];finish[k]=last
  if 'makespan' in d:assert type(d['makespan']) in (int,float) and d['makespan']==finish[-1], 'Reported makespan mismatch'
  return {'valid':True,'makespan':finish[-1],'n':n,'m':m}
 except Exception as e:return {'valid':False,'error':type(e).__name__+': '+str(e)}

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--instance',required=True);p.add_argument('--solution',required=True);a=p.parse_args();r=validate(a.instance,a.solution);print(json.dumps(r));raise SystemExit(0 if r['valid'] else 1)
