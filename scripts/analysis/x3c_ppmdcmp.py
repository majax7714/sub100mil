#!/usr/bin/env python3
"""X3c: region costs of full-scale `cmix --ppmd-only` runs (the real PPMD component alone, order 25 / 14 GB) from
their progress.log ("percent-of-coded-stream  archive-bytes-so-far", 0.01% steps), for streams in different article
orders.   python3 -P -E scripts/analysis/x3c_ppmdcmp.py NAME=STREAM:RUNDIR ...
STREAM = the coded (WRT) stream of that order (to locate the metadata / interlanguage blocks); RUNDIR holds
progress.log and out.bin, or is a build/exp/X3c/ppmd/<name> prefix (x3c_ppmdonly.sh). Prints text / metadata / interlanguage / total bytes per run and deltas to the first.
"""
import mmap
import os
import sys

import numpy as np

PAT = b"  L\xdf\x98N\n    L\xdf\x9bN"; PEND = b"  L/\xdf\x98N\n"; CEND = b"/\xdf\x8fN\n"; TEND = b"L/\xdf\xa7N\n"


def bounds(stream):
    f = open(stream, "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ); n = len(m)
    x = m.find(b" xmlnsM", max(0, n - 2_000_000))           # intro (after the blocks)
    il1 = m.rfind(TEND, 0, x) + len(TEND)
    meta1 = m.rfind(CEND, 0, il1) + len(CEND)
    # the metadata block starts after the last main page: search back for the last page end before the block
    lo = max(0, meta1 - 40_000_000)
    meta0 = m.rfind(PEND, lo, meta1) + len(PEND)
    return meta0, meta1, il1, n


def main():
    base = None
    print("run\ttext B\tmetadata B\tinterlanguage B\trest B\ttotal B (out.bin)\tdone %")
    for arg in sys.argv[1:]:
        name, rest = arg.split("="); stream, d = rest.split(":")
        meta0, meta1, il1, n = bounds(stream)
        if os.path.isdir(d):
            prog, szf = os.path.join(d, "progress.log"), None
        else:  # build/exp/X3c/ppmd/<name> prefix written by x3c_ppmdonly.sh
            prog, szf = d + ".progress.log", d + ".size"
        pr = np.loadtxt(prog)
        pos = pr[:, 0] / 100.0 * n; by = pr[:, 1]
        done = pr[-1, 0]
        if szf:
            tot = int(open(szf).read())
        else:
            tot = os.path.getsize(os.path.join(d, "out.bin")) if done >= 100 else by[-1]
        c = [np.interp(x, pos, by) if x <= pos[-1] else float("nan") for x in (meta0, meta1, il1)]
        r = [c[0], c[1] - c[0], c[2] - c[1], tot - c[2], tot]
        if base is None:
            base = r
        print(f"{name}\t" + "\t".join(f"{v:,.0f}" for v in r) + f"\t{done:.2f}")
        if base is not r:
            print(f"  vs first\t" + "\t".join(f"{v - b:+,.0f}" for v, b in zip(r, base)))
        print(f"  bounds: text {meta0:,} meta {meta1 - meta0:,} interlanguage {il1 - meta1:,} rest {n - il1:,}")


if __name__ == "__main__":
    main()
