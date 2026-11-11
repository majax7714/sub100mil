#!/usr/bin/env python3
"""X3c: can the page-id delta of the metadata block be predicted from data the decoder already has, other than the
previous record? Record linkage / library-catalogue lens: page ids were handed out in creation order, and batches
were created alphabetically, so the already-decoded record whose TITLE is alphabetically nearest may have a nearby id.
Causal walk over the full run's record order; for each article record compares log2(1 + |pid - prediction|) for
  prev   : the previous record's page id (what the block's delta coding uses now)
  alpha  : the page id of the alphabetically nearest already-seen title (either side)
  best2  : the smaller distance of the two (a mixing upper bound; costs <= 1 bit of selection)
  python3 -P -E scripts/analysis/x3c_pidlaw.py
"""
import bisect
import mmap
import re

import numpy as np

f = open("data/raw/enwik9", "rb"); mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
titles = [m.group(1) for m in re.finditer(rb"<page>\n    <title>(.*?)</title>", mm)]
rows = [l.rstrip("\n").split("\t") for l in open("build/exp/X2/meta_pages.tsv").readlines()[1:]]
rows = [r for r in rows if r[2] != "None"]
seen_t = []; seen_p = []
d_prev = []; d_alpha = []; d_best = []
prev = 0
for r in rows:
    page, pid, redir = int(r[1]), int(r[2]), r[7] == "1"
    t = titles[page].lower()
    i = bisect.bisect_left(seen_t, t)
    cands = [seen_p[j] for j in (i - 1, i) if 0 <= j < len(seen_p)]
    a = min((abs(pid - c) for c in cands), default=pid)
    p = abs(pid - prev)
    if not redir:
        d_prev.append(p); d_alpha.append(a); d_best.append(min(a, p))
    seen_t.insert(i, t); seen_p.insert(i, pid); prev = pid
for name, d in (("prev", d_prev), ("alpha", d_alpha), ("best2", d_best)):
    d = np.array(d, dtype=np.float64); n = len(d)
    parts = np.array_split(np.log2(1 + d), 4)
    print(f"{name}: mean log2(1+|d|) {np.log2(1 + d).mean():.2f} bits over {n:,} article records "
          f"(quarters {' '.join(f'{x.mean():.2f}' for x in parts)}); |d| <= 16: {np.mean(d <= 16):.3f}, <= 256: {np.mean(d <= 256):.3f}")
