#!/usr/bin/env python3
"""X3c: per-page sums of the C3 full-run cost dump (streamed; never loads the 23 GB file).

Usage: python3 -P -E scripts/analysis/x3c_pages.py [--cost build/exp/FULL/cost_full.bin] --out build/exp/X3c/pages_full.npz

A "segment" starts at every coded occurrence of "  <page>\n    <title>" (PAT, the transformer's article separator);
segment 0 is whatever precedes the first page, and the last segment also holds everything after the last page
(metadata block, interlanguage block, intro, coda: split later with --tail info from x3c_tail.py).
Per segment: start offset, coded bytes, bits of cost / cost_tf / cost_fx / cost_ppmd, bytes with a cmix or fxcm
match (cm_len>=32 bits or fx_len>0) and min(cost_tf, cost) is not stored (order sensitivity is cost_tf - cost).
"""
import argparse
import numpy as np

PAT = b"  L\xdf\x98N\n    L\xdf\x9bN"
DT = np.dtype([("byte", "u1"), ("cm_len", "u1"), ("cm_pred", "u1"), ("fx_len", "u1"), ("fx_pred", "u1"),
               ("sm_len", "u1"), ("sm_pred", "u1"), ("flags", "u1"), ("pbit", "<u2", 8), ("cost", "<f4"),
               ("cost_tf", "<f4"), ("cost_fx", "<f4"), ("cost_ppmd", "<f4")])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cost", default="build/exp/FULL/cost_full.bin")
    ap.add_argument("--out", required=True)
    ap.add_argument("--chunk", type=int, default=4_000_000)
    a = ap.parse_args()
    r = np.memmap(a.cost, dtype=DT, mode="r", offset=16)
    n = len(r)
    # pass 1: page starts from the byte column
    starts = [0]
    ov = len(PAT) - 1
    for lo in range(0, n, a.chunk):
        hi = min(n, lo + a.chunk + ov)
        b = np.ascontiguousarray(r["byte"][lo:hi]).tobytes()
        i = b.find(PAT)
        while i >= 0:
            if lo + i > 0 and lo + i < lo + a.chunk:
                starts.append(lo + i)
            i = b.find(PAT, i + 1)
    starts = np.array(sorted(set(starts)), dtype=np.int64)
    ns = len(starts)
    print(f"{n:,} coded bytes, {ns - 1:,} page starts", flush=True)
    keys = ["cost", "cost_tf", "cost_fx", "cost_ppmd"]
    sums = {k: np.zeros(ns) for k in keys}
    matched = np.zeros(ns)
    win = 1 << 20
    nw = (n + win - 1) // win
    wsum = {k: np.zeros(nw) for k in keys}
    for lo in range(0, n, a.chunk):
        hi = min(n, lo + a.chunk)
        c = np.array(r[lo:hi])
        seg = np.searchsorted(starts, np.arange(lo, hi), side="right") - 1
        w = np.arange(lo, hi) >> 20
        for k in keys:
            v = c[k].astype(np.float64)
            sums[k] += np.bincount(seg, weights=v, minlength=ns)
            wsum[k] += np.bincount(w, weights=v, minlength=nw)
        m = ((c["cm_len"] >= 32) | (c["fx_len"] > 0)).astype(np.float64)
        matched += np.bincount(seg, weights=m, minlength=ns)
    length = np.diff(np.append(starts, n))
    np.savez(a.out, starts=starts, length=length, matched=matched, n=n,
             **sums, **{"w_" + k: v for k, v in wsum.items()})
    for k in keys:
        print(f"{k}: {sums[k].sum() / 8:,.0f} B")


if __name__ == "__main__":
    main()
