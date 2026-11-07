# T1: compare a proxy's interwiki groups with the full run's (writes build/exp/T1/full_groups.pkl on first use).
# Usage (repo root): python3 -P -E scripts/analysis/t1_check_proxy.py PROXY.pre
import sys
src=open('scripts/analysis/t1_iw.py').read().replace('\nmain()\n','\n'); ns={}; exec(compile(src,'t1iw','exec'),ns)
import pickle,os
cache='build/exp/T1/full_groups.pkl'
if os.path.exists(cache): ref=pickle.load(open(cache,'rb'))
else:
    d=open('build/exp/A1/full/full.pre','rb').read(); t,g=ns['parse'](d)
    ref={ti:d[s:e] for ti,(s,e) in zip(t,g)}; pickle.dump(ref,open(cache,'wb'))
d=open(sys.argv[1],'rb').read(); t,g=ns['parse'](d)
same=sum(ref.get(ti)==d[s:e] for ti,(s,e) in zip(t,g))
print(sys.argv[1],'groups',len(g),'titles',len(t),'identical to full run groups',same)
