# T1: full-run interlanguage class counts per 10K reordered-rank bin (needs build/exp/T1/full_groups.pkl).
# Usage (repo root): python3 -P -E scripts/analysis/t1_rank_bins.py > build/exp/T1/rank_bins.tsv
import re,pickle,sys
from collections import defaultdict
src=open('scripts/analysis/t1_iw.py').read().replace('\nmain()\n','\n'); ns={}; exec(compile(src,'t1iw','exec'),ns)
ref=pickle.load(open('build/exp/T1/full_groups.pkl','rb'))
d=open('build/exp/A1/full/full.pre','rb').read()
last=d.rfind(b'</page>\n')
titles=[m.group(1) for m in re.finditer(rb'  <page>\n    <title>(.*?)</title>',d)]
print('pages',len(titles),file=sys.stderr)
R=ns['RE_LINK']; rel=ns['rel']
B=defaultdict(lambda: defaultdict(int))
rank_of={}
for line in open('build/exp/X2/meta_pages.tsv').readlines()[1:]:
    f=line.split('\t'); rank,page=int(f[0]),int(f[1])
    t=titles[page]; g=ref.get(t)
    b=B[rank//10000]
    b['pages']+=1
    if g is None: continue
    b['groups']+=1
    seen=[]
    for m in R.finditer(g+b'</text>'):
        x=m.group(2); r=rel(x,t,seen); b['links']+=1; b[r]+=1; seen.append(x)
keys=['pages','groups','links','eqT','eqT_dia','eqT_noparen','lcpT4','copy','other']
print('bin\t'+'\t'.join(keys))
for k in sorted(B): print(f'{k*10000}\t'+'\t'.join(str(B[k][x]) for x in keys))
