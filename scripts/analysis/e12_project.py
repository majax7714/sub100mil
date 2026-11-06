#!/usr/bin/env python3
"""E12 full-enwik9 projection [ESTIMATE].

Usage:
  e12_project.py SIM_BYCD.txt AUDIT_FULL.tsv [--deflate 1.3] [--minslots 20] [--extra SIM_BYCD2.txt:class,...]

SIM_BYCD.txt: stdout of `e12_sim ... bycd=1` (the per (class, log2 D bucket) table at its end).
AUDIT_FULL.tsv: written by `e12_slots.py --pre full.pre --audit`, slot counts per (class, log2 D bucket) in full enwik9.
For every full-scale cell, the simulated gain per slot of the same cell is used (or the nearest D bucket of the same
class that has at least --minslots slots). --extra takes the per-slot gains of the listed classes from another sim
run (e.g. `conv` from the 10M slice). --deflate divides the gain (the warm full-scale CM pays less per byte).
"""
import argparse
from collections import defaultdict


def read_sim(path):
    cells = {}
    on = False
    for line in open(path):
        if line.startswith("class\tdbucket"):
            on = True
            continue
        if on and line.strip():
            c, b, n, rec, base, full = line.split("\t")
            cells[(c, int(b))] = (int(n), float(rec), float(base), float(full))
    return cells


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sim")
    ap.add_argument("audit")
    ap.add_argument("--deflate", type=float, default=1.0)
    ap.add_argument("--minslots", type=int, default=20)
    ap.add_argument("--extra", action="append", default=[])
    args = ap.parse_args()
    sim = read_sim(args.sim)
    for e in args.extra:
        path, classes = e.split(":")
        other = read_sim(path)
        for k, v in other.items():
            if k[0] in classes.split(","):
                sim[k] = v
    full = defaultdict(int)
    for line in list(open(args.audit))[1:]:
        c, b, n = line.split("\t")[:3]
        full[(c, int(b))] += int(n)
    bycls = defaultdict(lambda: [0, 0.0, 0.0, 0])  # full slots, gain bytes, record bytes, unmatched slots
    for (c, b), n in full.items():
        cand = [(abs(bb - b), bb) for (cc, bb), v in sim.items() if cc == c and v[0] >= args.minslots]
        if not cand:
            bycls[c][0] += n
            bycls[c][3] += n
            continue
        _, bb = min(cand)
        sn, rec, base, fl = sim[(c, bb)]
        bycls[c][0] += n
        bycls[c][1] += n * (base - fl) / sn / 8 / args.deflate
        bycls[c][2] += n * rec / sn / 8 / args.deflate
    tot_g = tot_r = 0
    print(f"{'class':12s} {'full slots':>11s} {'record KB':>10s} {'gain KB':>9s} {'no sim':>7s}")
    for c, (n, g, r, u) in sorted(bycls.items(), key=lambda kv: -kv[1][1]):
        print(f"{c:12s} {n:11,d} {r / 1000:10.1f} {g / 1000:9.1f} {u:7d}")
        tot_g += g
        tot_r += r
    print(f"{'total':12s} {sum(v[0] for v in bycls.values()):11,d} {tot_r / 1000:10.1f} {tot_g / 1000:9.1f}")


if __name__ == "__main__":
    main()
