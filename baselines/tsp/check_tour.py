"""Independent TSPLIB EUC_2D tour validator. Contains no solving algorithm."""
import argparse
import json
import math
from pathlib import Path

def read_instance(path):
    header={};coords={};section=False
    for raw in Path(path).read_text(encoding='utf-8-sig').splitlines():
        line=raw.strip()
        if not line:continue
        if line=='NODE_COORD_SECTION':section=True;continue
        if line=='EOF':break
        if section:
            values=line.split()
            if len(values)!=3:raise ValueError('Expected node id, x, y')
            node=int(values[0]);xy=tuple(map(float,values[1:]))
            if node in coords or not all(math.isfinite(x) for x in xy):raise ValueError('Invalid coordinate')
            coords[node]=xy
        elif ':' in line:
            k,v=line.split(':',1);header[k.strip()]=v.strip()
    if header.get('TYPE')!='TSP' or header.get('EDGE_WEIGHT_TYPE')!='EUC_2D':raise ValueError('Only TSP EUC_2D supported')
    if int(header['DIMENSION'])!=len(coords):raise ValueError('Dimension mismatch')
    return coords

def read_tour(path):
    lines=Path(path).read_text(encoding='utf-8-sig').splitlines();tour=[];section=False;ended=False
    for raw in lines:
        line=raw.strip()
        if line=='TOUR_SECTION':
            if section:raise ValueError('Duplicate TOUR_SECTION')
            section=True;continue
        if not section:continue
        for token in line.split():
            if token in ['-1','EOF']:ended=True;continue
            if ended:raise ValueError('Data after tour terminator')
            tour.append(int(token))
    if not section or not ended:raise ValueError('Missing TOUR_SECTION or terminator')
    return tour

def validate(instance,tourfile):
    try:
        coords=read_instance(instance);tour=read_tour(tourfile)
        if len(tour)!=len(coords) or set(tour)!=set(coords):raise ValueError('Each city must occur exactly once')
        length=sum(math.floor(math.hypot(coords[a][0]-coords[b][0],coords[a][1]-coords[b][1])+0.5) for a,b in zip(tour,tour[1:]+tour[:1]))
        return {'valid':True,'cities':len(tour),'length':length}
    except Exception as e:return {'valid':False,'error':str(e)}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--instance',required=True);p.add_argument('--tour',required=True)
    a=p.parse_args();result=validate(a.instance,a.tour);print(json.dumps(result,indent=2));raise SystemExit(0 if result['valid'] else 1)
