#!/usr/bin/env python3
"""X3c side finding: X2's page-id tracking (x2_timelaw.h, end_line) only accepts a page-id delta line that is
followed IMMEDIATELY by "revision>". Pages with a <restrictions> element put a "restrictions>..." line in between, so
their delta is dropped and the tracked page id drifts; try_rpid() then indexes used_[prev_pid_ + 1] without a
lower bound (segfault when negative; seen on a record-order sample of the first pages, x3c A1_rec).
Replays that logic over a slicetool-pre / -e stream and reports the drift.
  python3 -P -E scripts/analysis/x3c_x2drift.py build/exp/E12/full/.ready4cmix
"""
import mmap
import sys

f = open(sys.argv[1], "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
start = m.rfind(b"  </page>\n") + 10
end = m.find(b"[", start)  # first interlanguage link (the block has no '[')
blk = m[start:end].split(b"\n")
have = False; pend = False; pd = 0; pid = 0; true_pid = 0; good = 0
n = miss = calls = neg_calls = mis_calls = 0; first_neg = None; worst = 0
prev = b""
for L in blk:
    was = pend; pend = False
    isd = 2 <= len(L) < 16 and L[:1] == b">" and (L[1:].isdigit() or (L[1:2] == b"-" and L[2:].isdigit()))
    if isd and prev == b"/contributor>" and have and good >= 4:
        calls += 1  # try_rpid() fires when the '>' of this line arrives
        if pid + 1 < 0:
            neg_calls += 1
            if first_neg is None:
                first_neg = (n, pid, true_pid)
        if pid != true_pid:
            mis_calls += 1
    if isd and (prev == b"/contributor>" or not have):
        pend = True; pd = int(L[1:]); true_pid += pd; n += 1
    elif L == b"revision>":
        if was:
            pid = (pid if have else 0) + pd
            good = good + 1 if (have and 1 <= pd <= 64) else 0
            have = True
        else:
            miss += 1
    worst = min(worst, pid)
    prev = L
print(f"records {n:,}; deltas dropped (a line between delta and revision>) {miss:,}; final drift {pid - true_pid:+,}; "
      f"lowest tracked page id {worst:,}")
print(f"try_rpid calls {calls:,}; with a drifted base {mis_calls:,}; with prev_pid_ + 1 < 0 (out-of-bounds read) {neg_calls:,}"
      + (f"; first at record {first_neg[0]:,} (tracked {first_neg[1]:,}, true {first_neg[2]:,})" if first_neg else ""))
