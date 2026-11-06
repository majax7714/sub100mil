#!/usr/bin/env python3
"""X2: how well does an online revision-id -> time calibration predict each page's timestamp in the metadata block?

Usage: python3 -P -E scripts/analysis/x2_meta_interp.py [--pre build/exp/A1/full/full.pre] [--tail-bytes 60000000]
Walks the metadata block in order. For record k the specialist knows records 0..k-1 (as a decoder would), finds the
nearest known revision ids below and above, and interpolates the time linearly. Reports the error distribution and
the ideal code length of the timestamp under an adaptive model of the error (sign + log2 bucket with adaptive
counts per gap bucket, uniform inside the bucket), by position in the block.
"""
import argparse, bisect, math, re
from collections import defaultdict

RE_REC = re.compile(rb">(\d+)\nrevision>\n(?:restrictions>[^\n]*\n)?id>(\d+)\ntimestamp>0(\d)(\d+):(\d+)\ncontributor>\n(?:username>([^\n]*)\nid>(\d+)|ip>([^\n]*))\n/contributor>\n")


def tsec(y, d, s):
    return ((y * 372) + d) * 86400 + s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", default="build/exp/A1/full/full.pre")
    ap.add_argument("--tail-bytes", type=int, default=60_000_000)
    a = ap.parse_args()
    with open(a.pre, "rb") as f:
        f.seek(0, 2); n = f.tell(); f.seek(max(0, n - a.tail_bytes)); t = f.read()
    e = t.rfind(b"  </page>\n") + 10
    recs = [(int(m.group(2)), tsec(int(m.group(3)), int(m.group(4)), int(m.group(5))), m.group(6) or m.group(8))
            for m in RE_REC.finditer(t, e)]
    print(f"{len(recs):,} records")
    keys, vals = [], []
    NB = 40
    cnt = defaultdict(lambda: [1.0] * (2 * NB + 1))
    stats = defaultdict(lambda: [0, 0.0, 0])  # decile -> n, bits, |err|<=60
    errs = []
    for k, (r, T, user) in enumerate(recs):
        i = bisect.bisect_left(keys, r)
        if 0 < i < len(keys):
            r0, r1, t0, t1 = keys[i - 1], keys[i], vals[i - 1], vals[i]
            pred = t0 + (t1 - t0) * (r - r0) / (r1 - r0) if r1 > r0 else t0
            gap = r1 - r0
        elif keys:
            j = i - 1 if i > 0 else 0
            pred, gap = vals[j], 10 ** 8
        else:
            pred, gap = 0, 10 ** 9
        err = int(round(T - pred))
        gb = min(int(math.log2(gap + 1)), 31)
        b = 0 if err == 0 else (1 + min(int(math.log2(abs(err))), NB - 1))
        sym = NB + (b if err > 0 else -b)
        c = cnt[gb]
        p = c[sym] / sum(c)
        bits = -math.log2(p) + (b - 1 if b > 1 else 0)  # uniform inside [2^(b-1), 2^b)
        c[sym] += 1.0
        dec = min(9, k * 10 // len(recs))
        st = stats[dec]; st[0] += 1; st[1] += bits; st[2] += abs(err) <= 60
        errs.append(abs(err))
        bisect.insort(keys, r); vals.insert(bisect.bisect_left(keys, r), T)
    tot = sum(s[1] for s in stats.values())
    print(f"ideal code length of the timestamp given the revision id: {tot / len(recs):.2f} bits/record, total {tot / 8:,.0f} B")
    for d in sorted(stats):
        n, b, w = stats[d]
        print(f"  decile {d}: {n:6d} recs  {b / n:6.2f} bits/ts  |err|<=60s {100 * w / n:5.1f}%")
    errs.sort()
    for q in (0.1, 0.25, 0.5, 0.75, 0.9, 0.95):
        print(f"  |err| quantile {q}: {errs[int(q * len(errs))]} s")


main()
