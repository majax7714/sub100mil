# T1: census category derivations (lead link / "type in STATE"). Usage: python3 -P -E scripts/analysis/t1_census_cat.py PRE [COSTLOG_BASENAME]
import re,sys,os
import numpy as np
sys.path.insert(0,'scripts/analysis')
from collections import defaultdict
d=open(sys.argv[1],'rb').read()
cc=np.load('build/exp/X2/tcost_'+sys.argv[2]+'.npy') if len(sys.argv)>2 else None
lim=len(cc)-2 if cc is not None else len(d)
last=d.rfind(b'  </page>\n')
st=[m.start() for m in re.finditer(rb"  <page>\n", d[:last])]+[last+10]
R=defaultdict(lambda:[0,0,0.0])
ncen=0
for i in range(len(st)-1):
    s,e=st[i],st[i+1]
    if e>lim: break
    p=d[s:e]
    if b'census, the ' not in p or b'As of the [2000] census' not in p: continue
    ncen+=1
    t=re.search(rb'<title>(.*?)</title>',p).group(1)
    state=t.split(b', ')[-1]
    lead=p[:p.find(b'census')]
    links=set(re.findall(rb'\[([^\]|\n]+)',lead))
    m=re.search(rb"''' is an? ([^\[]*?)(?:located )?in \[",lead)
    for m2 in re.finditer(rb'\[Category:([^\]\n]*)\]',p):
        nm=m2.group(1)
        k='other'
        if nm in links: k='=lead link'
        elif nm.endswith(b' in '+state) or nm.endswith(b' of '+state): k='type in STATE'
        A=R[k]; A[0]+=1; A[1]+=len(nm)
        if cc is not None: A[2]+=cc[s+m2.end(1)+1]-cc[s+m2.start(1)]
print('census pages',ncen)
for k,(n,b,c) in R.items(): print(f'{k:16s} {n:8,d} {b:9,d} B  rec {c/8:8,.0f} B  {c/max(n,1):6.2f} bits/inst')
