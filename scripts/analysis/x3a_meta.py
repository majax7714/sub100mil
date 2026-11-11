#!/usr/bin/env python3
"""X3a: per-field cost of phda9's metadata block in the C3 full run (reordered, warm, after E12+X2).
Input: build/exp/X3a/full.txt / full.cost from x3a_decode. Writes meta_records.tsv (one row per record).
  python3 -P -E scripts/analysis/x3a_meta.py [--tsv build/exp/X3a/meta_records.tsv]
"""
import mmap, re, sys, argparse, collections
import numpy as np
ap = argparse.ArgumentParser(); ap.add_argument('--dir', default='build/exp/X3a'); ap.add_argument('--tsv')
a = ap.parse_args()
f = open(a.dir + '/full.txt', 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
cost = np.memmap(a.dir + '/full.cost', dtype='<f4', mode='r')
ms = m.find(b'  </page>\n', 894000000)
ms = m.rfind(b'  </page>\n', 0, m.find(b'\nrevision>\nid>', ms)) + len(b'  </page>\n')
me = m.find(b'\n[', ms) + 1   # interlanguage block starts at the first line beginning with '['
blk = m[ms:me]; c = np.asarray(cost[ms:me], dtype=np.float64)
cs = np.concatenate([[0], np.cumsum(c)])
print('metadata block', ms, me, 'bytes', me - ms, 'C3 cost B', cs[-1] / 8)
print('head', repr(blk[:40]))
fields = collections.Counter(); cnt = collections.Counter()
pos = 0; lines = blk.split(b'\n'); recs = []; cur = None
off = 0
for ln in lines:
    L = len(ln) + 1; bits = cs[min(off + L, len(c))] - cs[off]
    if ln.startswith(b'>'): k = 'pid_delta'; cur = {'pid': ln[1:].decode()}; recs.append(cur)
    elif ln == b'revision>': k = 'tag_revision'
    elif ln.startswith(b'id>'):
        if cur is not None and 'rev' not in cur: k = 'rev_id'; cur['rev'] = ln[3:].decode()
        else: k = 'user_id'; cur['uid'] = ln[3:].decode()
    elif ln.startswith(b'timestamp>'): k = 'timestamp'; cur['ts'] = ln[10:].decode()
    elif ln == b'contributor>': k = 'tag_contributor'
    elif ln.startswith(b'username>'): k = 'username'; cur['user'] = ln[9:].decode('utf8', 'replace')
    elif ln.startswith(b'ip>'): k = 'ip'; cur['ip'] = ln[3:].decode('utf8', 'replace')
    elif ln == b'/contributor>': k = 'tag_/contributor'
    elif ln.startswith(b'restrictions>'): k = 'restrictions'; cur['restr'] = ln[13:].decode()
    else: k = 'other'
    if cur is not None: cur[k + '_bits'] = cur.get(k + '_bits', 0) + bits
    fields[k] += bits; cnt[k] += 1; off += L
print('records', len(recs))
for k, v in sorted(fields.items(), key=lambda x: -x[1]):
    print(f'{k:18s} n={cnt[k]:8d} {v/8/1000:9.1f} KB  {v/cnt[k]:6.2f} bits each')
if a.tsv:
    cols = ['pid', 'rev', 'ts', 'user', 'uid', 'ip', 'restr', 'pid_delta_bits', 'rev_id_bits', 'timestamp_bits', 'username_bits', 'user_id_bits', 'ip_bits']
    with open(a.tsv, 'w') as o:
        o.write('\t'.join(cols) + '\n')
        for r in recs:
            o.write('\t'.join(str(round(r[k], 3)) if isinstance(r.get(k), float) else str(r.get(k, '')) for k in cols) + '\n')
