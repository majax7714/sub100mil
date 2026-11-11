#!/usr/bin/env python3
"""X3a: per-page table of the C3 full-run main stream (reordered order), with C3's cost per part.
Writes build/exp/X3a/pages.tsv: start, title, minor, comment, redirect target, text length, and bits paid on
title / comment / text. Page k here owns metadata record k (meta_records.tsv).
"""
import mmap, re, sys
import numpy as np
D = 'build/exp/X3a'
f = open(D + '/full.txt', 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
cost = np.memmap(D + '/full.cost', dtype='<f4', mode='r')
end = m.find(b'\nrevision>\nid>', 894000000)
pg = re.compile(rb'  <page>\n    <title>([^\n]*)</title>\n(      <minor />\n)?(?:      <comment>(.*?)</comment>\n)?      <text xml:space="preserve"', re.S)
starts = []
for mm in pg.finditer(m, 0, end):
    starts.append((mm.start(), mm.start(1), mm.end(1), mm.group(1), bool(mm.group(2)), mm.group(3), mm.start(3), mm.end(3), mm.end()))
print('pages', len(starts), file=sys.stderr)
def S(a, b): return float(np.asarray(cost[a:b], dtype=np.float64).sum())
with open(D + '/pages.tsv', 'w') as o:
    o.write('start\ttitle\tminor\thascomment\tcomment\tredirect\ttextlen\ttitle_bits\tcomment_bits\ttext_bits\n')
    for i, (s, t0, t1, title, minor, com, c0, c1, te) in enumerate(starts):
        nxt = starts[i + 1][0] if i + 1 < len(starts) else end
        head = m[te:te + 300]
        r = re.match(rb'>#REDIRECT\s*:?\s*\[([^\]\n]*)\]', head, re.I)
        esc = lambda b: b.decode('utf8', 'replace').replace('\t', ' ').replace('\n', '\\n').replace('\r', ' ')
        o.write(f"{s}\t{esc(title)}\t{int(minor)}\t{int(com is not None)}\t{esc(com) if com is not None else ''}\t{esc(r.group(1)) if r else ''}\t{nxt - te}\t"
                f"{S(t0, t1):.2f}\t{S(c0 - 9, c1 + 11) if com is not None else 0:.2f}\t{S(te, nxt):.2f}\n")
