#!/usr/bin/env python3
"""x2fix: what the C3 full run really paid for the redirect page-id fields (the digits and newline after a record's
'>', i.e. X2's rpid slot), from the full-run cost dump, per rank window. Compare with the cold proxies' cost of the same
fields (x2_csim: "class rpid: slots N, a -> b B").
  python3 -P -E scripts/analysis/x2fix_fullcost.py build/exp/E12/full/e9.stream build/exp/FULL/cost_full.bin build/exp/E12/full/.ready4cmix
The coded stream is the WRT of .ready4cmix: '>' is coded 'N', digits, '-' and '\\n' are themselves. The delta lines
are found in the stream's tail and checked against the block's delta list.
"""
import mmap
import re
import sys

import numpy as np

st, cost, pre = sys.argv[1:4]
f = open(pre, "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
start = m.rfind(b"  </page>\n") + 10
end = m.find(b"[", start)
blk = m[start:end].split(b"\n")
deltas = []; prev = b""
for L in blk:
    if 2 <= len(L) < 16 and L[:1] == b">" and (L[1:].isdigit() or (L[1:2] == b"-" and L[2:].isdigit())) and \
            (prev == b"/contributor>" or not deltas):
        deltas.append(L[1:])
    prev = L
s = open(st, "rb"); sm = mmap.mmap(s.fileno(), 0, access=mmap.ACCESS_READ)
n = len(sm)
tail0 = n - 40_000_000
tail = sm[tail0:]
# the first record's line, then every later one
first = tail.find(b"\nN" + deltas[0] + b"\n")
assert first >= 0
pos = []; p = first
for d in deltas:
    pat = b"\nN" + d + b"\n"
    q = tail.find(pat, p)
    assert q >= 0 and q - p < 4096, (len(pos), d, q - p)
    pos.append((tail0 + q + 2, len(d) + 1))  # the field: digits + newline
    p = q + len(pat) - 1
print(f"{len(pos):,} page-id fields located in the coded stream (block starts near byte {tail0 + first:,} of {n:,})")
c = np.memmap(cost, dtype=np.dtype([("b", "u1", 24), ("cost", "<f4"), ("o", "<f4", 3)]), mode="r", offset=16)
assert len(c) == n
bits = np.array([float(c["cost"][a:a + l].sum()) for a, l in pos])
gt = np.array([c["b"][a - 1, 0] for a, _ in pos[:1000]])
assert (gt == ord("N")).all()
R0 = 172282
print(f"article part (records 1..{R0 - 1:,}): {bits[1:R0].mean():.3f} bits/field, {bits[1:R0].sum() / 8e3:.1f} KB")
print(f"redirect part (records {R0:,}..): {bits[R0:].mean():.3f} bits/field, {bits[R0:].sum() / 8e3:.1f} KB over {len(bits) - R0:,} fields")
for lo in list(range(170000, len(bits), 10000)):
    w = bits[max(lo, R0):lo + 10000]
    print(f"  ranks {lo:,}+: {w.mean():.3f} bits/field")
for lo in (175000, 200000, 230000):
    print(f"  proxy window {lo:,}..{lo + 2800:,}: {bits[lo:lo + 2800].mean():.3f} bits/field")
