#!/usr/bin/env python3
"""X3c: is the frozen transformer order-sensitive? Compare each article's transformer-alone cost (cost_tf) between
two cost logs that code the same articles in different orders / histories.

  x3c_tfsens.py full  COST_SLICE.bin SAMPLE.pages     slice run vs the C3 full run (build/exp/X3c/pages_full.npz)
  x3c_tfsens.py pair  COST_A.bin A.pages COST_B.bin B.pages
SAMPLE.pages = page indices in the sample's order (x3c_mksample.py writes it; for data/slices/enwik9_10M use "id").
Prints totals over the common articles for cost_tf, final cost, G = cost_tf - cost, and per-article spread.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
PAT = b"  L\xdf\x98N\n    L\xdf\x9bN"
DT = np.dtype([("byte", "u1"), ("x", "u1", 7), ("pbit", "<u2", 8), ("cost", "<f4"), ("cost_tf", "<f4"),
               ("cost_fx", "<f4"), ("cost_ppmd", "<f4")])
KEYS = ("cost", "cost_tf", "cost_fx", "cost_ppmd")


def per_page(path, pages_file):
    r = np.fromfile(path, dtype=DT, offset=16)
    b = r["byte"].tobytes()
    st = []
    i = b.find(PAT)
    while i >= 0:
        st.append(i); i = b.find(PAT, i + 1)
    st = np.array(st)
    seg = np.searchsorted(st, np.arange(len(r)), side="right")  # 0 = before first page
    out = {k: np.bincount(seg, weights=r[k].astype(np.float64), minlength=len(st) + 1)[1:] for k in KEYS}
    out["len"] = np.diff(np.append(st, len(r)))
    if pages_file == "id":
        pages = np.arange(len(st))
    else:
        pages = np.array([int(x) for x in open(pages_file).read().split()])
    n = min(len(pages), len(st))
    # the last segment also holds the tail blocks: drop it
    return {int(pages[i]): {k: out[k][i] for k in out} for i in range(n - 1)}


def full_pages():
    z = np.load("build/exp/X3c/pages_full.npz"); idx = np.load("build/exp/X3c/page_index.npz")
    par = idx["page_at_rank"].tolist()
    a = {k: z[k].tolist() for k in KEYS}; ln = z["length"].tolist()
    return {par[i]: {"len": ln[i + 1], **{k: a[k][i + 1] for k in KEYS}} for i in range(len(par) - 1)}


def report(A, B, na, nb):
    common = [p for p in A if p in B and A[p]["len"] == B[p]["len"]]
    print(f"{len(common)} common articles with equal coded length (of {len(A)} / {len(B)})")
    L = sum(A[p]["len"] for p in common)
    print(f"coded bytes {L:,}")
    for k in KEYS:
        a = np.array([A[p][k] for p in common]); b = np.array([B[p][k] for p in common])
        print(f"{k:10s} {na}: {a.sum() / 8:12,.0f} B   {nb}: {b.sum() / 8:12,.0f} B   {nb}-{na}: {(b.sum() - a.sum()) / 8:+10,.0f} B "
              f"({100 * (b.sum() - a.sum()) / a.sum():+.3f}%)  per-article |d| sum {np.abs(b - a).sum() / 8:,.0f} B")
    ga = np.array([A[p]["cost_tf"] - A[p]["cost"] for p in common]); gb = np.array([B[p]["cost_tf"] - B[p]["cost"] for p in common])
    print(f"G=tf-final {na}: {ga.sum() / 8:,.0f} B   {nb}: {gb.sum() / 8:,.0f} B")
    return common


if __name__ == "__main__":
    if sys.argv[1] == "full":
        report(per_page(sys.argv[2], sys.argv[3]), full_pages(), "slice", "full")
    else:
        report(per_page(sys.argv[2], sys.argv[3]), per_page(sys.argv[4], sys.argv[5]), "A", "B")
