#!/usr/bin/env python3
"""x2fix: replay X2's redirect page-id law (x2_timelaw.h, try_rpid) over the full run's metadata block, with the
page-id tracking as C3/C4 shipped it ("old": the delta of a page with a "restrictions>" line is dropped, so the
tracked id and the used-id bitmap drift) and as patches/exp/C4-x2fix tracks it ("new": exact).

For every slot it finds the index j of the true delta among the law's 64 candidates (next unused ids), and prices
the law ALONE: -log2(w_j / sum w), w_j = (1 + j)^-1.5 (a slot whose true delta is not a candidate is a miss). That is
not the coded cost (the real stage mixes the law with the model's own p); x2fix_csim.cpp does that on proxies.

  python3 -P -E scripts/analysis/x2fix_replay.py build/exp/E12/full/.ready4cmix [--seed-dir DIR --ranks 175000,200000,230000]

--seed-dir: for each rank R write DIR/seed_old_R.tsv, rows "rev T pos" for the records before rank R, where pos is
the bitmap position the OLD tracking marked for that record, shifted by minus the drift at rank R (-1 if the record
was not marked). x2_csim/x2fix_csim seeded with it and built on the OLD header see, on the proxy of ranks R.., the
redirect law's state of the real full run (the candidates are deltas, so a common shift does not change them).
The time law's seeds (rev, T) are the true ones, as in x2_meta2.py --seed-out (T = (372 (y - 2001) + day) 86400 + s).
"""
import argparse
import math
import mmap

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("stream")
ap.add_argument("--seed-dir")
ap.add_argument("--ranks", default="175000,200000,230000")
a = ap.parse_args()

f = open(a.stream, "rb")
m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
start = m.rfind(b"  </page>\n") + 10
end = m.find(b"[", start)  # first interlanguage link (the block has no '[')
blk = m[start:end].split(b"\n")

W = np.array([(1.0 + j) ** -1.5 for j in range(64)])
WS = W.sum()
SIZE = 60_000_000


class Track:
    def __init__(self, fixed):
        self.fixed = fixed
        self.have = False
        self.pend = False
        self.pd = 0
        self.pid = 0
        self.good = 0
        self.used = np.zeros(SIZE, dtype=bool)
        self.slots = []  # (record index, candidate index or -1)
        self.marks = []  # per record: marked position or -1
        self.lowest = 0

    def cand_index(self, d):
        """index of delta d among the next 64 unused ids after self.pid, or -1"""
        x = self.pid + 1
        if x < 0:
            raise RuntimeError("tracked id below -1: the old code reads the bitmap out of bounds here")
        tgt = self.pid + d
        got = 0
        while got < 64:
            seg = ~self.used[x:x + 4096]
            free = np.flatnonzero(seg)
            for o in free[:64 - got]:
                if x + o == tgt:
                    return got
                got += 1
            x += 4096
        return -1


def is_delta(L):
    return 2 <= len(L) < 16 and L[:1] == b">" and (L[1:].isdigit() or (L[1:2] == b"-" and L[2:].isdigit()))


old, new = Track(False), Track(True)
true_pid = 0
recs = []  # (rev, T, true pid, drift of the old tracking after this record)
n = 0
prev = b""
rev = -1
want_rev = False
for L in blk:
    isd = is_delta(L)
    for t in (old, new):
        was = t.pend
        t.pend = False
        if isd and prev == b"/contributor>" and t.have and t.good >= 4:
            t.slots.append((n, t.cand_index(int(L[1:]))))  # try_rpid() at this line's '>'
        if isd and (prev == b"/contributor>" or not t.have):
            t.pend = True
            t.pd = int(L[1:])
        elif t.fixed and was and L.startswith(b"restrictions>"):
            t.pend = True
        elif L == b"revision>":
            if was:
                t.pid = (t.pid if t.have else 0) + t.pd
                t.good = t.good + 1 if (t.have and 1 <= t.pd <= 64) else 0
                t.have = True
                if 0 <= t.pid < SIZE:
                    t.used[t.pid] = True
                t.marks.append(t.pid)
            else:
                t.marks.append(-1)
            t.lowest = min(t.lowest, t.pid)
    if isd and (prev == b"/contributor>" or n == 0):
        true_pid += int(L[1:])
        n += 1
    elif L == b"revision>":
        want_rev = True
        rev = -1
    elif want_rev and L.startswith(b"id>") and L[3:].isdigit():
        rev = int(L[3:])
        want_rev = False
    elif L.startswith(b"timestamp>0"):
        y = L[11] - 48
        d, s = L[12:].split(b":")
        recs.append((rev, (y * 372 + int(d)) * 86400 + int(s), true_pid, old.pid - true_pid))
    prev = L

assert len(recs) == n == len(old.marks) == len(new.marks), (len(recs), n, len(old.marks), len(new.marks))
assert new.pid == true_pid
print(f"records {n:,}; old tracking: final drift {old.pid - true_pid:+,}, lowest tracked id {old.lowest:,}, "
      f"records not marked {sum(1 for x in old.marks if x < 0):,}; new tracking: drift {new.pid - true_pid:+,}, "
      f"not marked {sum(1 for x in new.marks if x < 0):,}")
for name, t in (("old", old), ("new", new)):
    j = np.array([c for _, c in t.slots])
    hit = j >= 0
    bits = -np.log2(W[j[hit]] / WS)
    drifted = sum(1 for r, _ in t.slots if (recs[r - 1][3] if r else 0) != 0) if name == "old" else 0
    print(f"{name}: slots {len(j):,} (first at record {t.slots[0][0]:,}); with a drifted base {drifted:,}; "
          f"true delta = candidate 0: {np.mean(j == 0) * 100:.1f}%, candidates 0-3: {np.mean((j >= 0) & (j < 4)) * 100:.1f}%, "
          f"not a candidate: {np.mean(~hit) * 100:.2f}%; law alone on the hits: {bits.mean():.3f} bits/slot "
          f"({bits.sum() / 8 / 1000:.1f} KB + {int((~hit).sum()):,} misses)")
# per rank window of 10,000 records
print("rank window: slots old/new, mean law-alone bits on hits old/new, misses old/new")
for lo in range(170000, n, 10000):
    row = []
    for t in (old, new):
        j = np.array([c for r, c in t.slots if lo <= r < lo + 10000])
        h = j >= 0
        row.append((len(j), float((-np.log2(W[j[h]] / WS)).mean()) if h.any() else math.nan, int((~h).sum())))
    print(f"  {lo:,}: {row[0][0]}/{row[1][0]}  {row[0][1]:.3f}/{row[1][1]:.3f}  {row[0][2]}/{row[1][2]}")

if a.seed_dir:
    for R in (int(x) for x in a.ranks.split(",")):
        D0 = recs[R - 1][3]
        k = 0
        with open(f"{a.seed_dir}/seed_old_{R}.tsv", "w") as o:
            for i in range(R):
                pos = old.marks[i] - D0 if old.marks[i] >= 0 else -1
                if pos < 0:
                    pos = -1
                    k += 1
                o.write(f"{recs[i][0]}\t{recs[i][1]}\t{pos}\n")
        print(f"seed_old_{R}.tsv: drift at rank {R:,} = {D0:+,}; {k:,} of {R:,} records without a bitmap position")
