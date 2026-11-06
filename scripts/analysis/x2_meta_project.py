#!/usr/bin/env python3
"""X2: project the metadata specialist's gain to the full run's block.

Usage:
  python3 -P -E scripts/analysis/x2_meta_project.py --meta build/exp/X2/meta_pages.tsv \
      --point 1400:SIM.txt --point 101400:SIM.txt ...   [--deflate 1.3]

Each --point is the middle rank of a proxy (a cost-logged record run on reordered ranks, see x2_proxy.py) and the
x2_sim output on it. Realized gain per slot (baseline - full, bits) is read per class (pid|revid = page-id delta,
ts = timestamp), interpolated linearly in rank between points (held constant beyond the ends), and summed over the
slots the specialist opens in the full block (the same causal walk as x2_meta2.py). uid is not counted: a warm CM
already knows a user's id after one occurrence.
"""
import argparse
import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x2_meta2 as m2  # noqa: E402


def read_sim(path):
    g = {}
    for line in open(path):
        mm = re.match(r"(pid\|revid|ts)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([+-]\d+)", line)
        if mm:
            n, base, full = int(mm.group(2)), int(mm.group(5)), int(mm.group(6))
            g["pid" if mm.group(1).startswith("pid") else "ts"] = (8.0 * (base - full) / n, n)
    return g


def interp(pts, r):
    if r <= pts[0][0]:
        return pts[0][1]
    for (r0, g0), (r1, g1) in zip(pts, pts[1:]):
        if r <= r1:
            return g0 + (g1 - g0) * (r - r0) / (r1 - r0)
    return pts[-1][1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--meta", required=True)
    ap.add_argument("--point", action="append", required=True)
    ap.add_argument("--deflate", type=float, default=1.0)
    a = ap.parse_args()
    pts = {"pid": [], "ts": []}
    for p in a.point:
        r, path = p.split(":", 1)
        g = read_sim(path)
        for f in pts:
            if f in g:
                pts[f].append((int(r), g[f][0]))
                print(f"point rank {int(r):7,d} {f:3s}: {g[f][0]:5.2f} bits/slot over {g[f][1]:,} slots")
    for f in pts:
        pts[f].sort()
    meta = m2.load_meta(a.meta)
    tot = {"pid": 0.0, "ts": 0.0}
    cnt = {"pid": 0, "ts": 0}
    for rank, row, pr in m2.walk(meta, 2000):
        for f in ("pid", "ts"):
            if f in pr and pts[f]:
                tot[f] += interp(pts[f], rank)
                cnt[f] += 1
    s = 0.0
    for f in ("pid", "ts"):
        kb = tot[f] / 8 / 1000 / a.deflate
        s += kb
        print(f"{f:3s}: {cnt[f]:,} slots in the full block, projected gain {kb:,.1f} KB"
              + (f" (deflated /{a.deflate})" if a.deflate != 1 else ""))
    print(f"total {s:,.1f} KB  [ESTIMATE]")


if __name__ == "__main__":
    main()
