#!/usr/bin/env python3
"""X3c: stakes for the arrangement candidates from the C3 full-run cost dump (streamed).
Per page (record order): bits on the title line, on the rest of the page; redirect flag (X2's table). Also the
cost by decile of the text region (curriculum view). Needs pages_full.npz (x3c_pages.py), e9.stream, meta_pages.tsv.

  python3 -P -E scripts/analysis/x3c_fields.py > build/exp/X3c/fields.txt
"""
import mmap
import numpy as np

DT = np.dtype([("byte", "u1"), ("x", "u1", 7), ("pbit", "<u2", 8), ("cost", "<f4"), ("cost_tf", "<f4"),
               ("cost_fx", "<f4"), ("cost_ppmd", "<f4")])
z = np.load("build/exp/X3c/pages_full.npz")
st = z["starts"]; n = int(z["n"]); ns = len(st)
f = open("build/exp/E12/full/e9.stream", "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
tend = np.zeros(ns, dtype=np.int64)
for i in range(1, ns):
    tend[i] = m.find(b"\n", int(st[i]) + 14) + 1
r = np.memmap("build/exp/FULL/cost_full.bin", dtype=DT, mode="r", offset=16)
tc = np.zeros(ns); ttf = np.zeros(ns)
CH = 4_000_000
for lo in range(0, int(st[-1]) + 4096, CH):
    hi = min(n, lo + CH)
    c = np.array(r[lo:hi]); idx = np.arange(lo, hi)
    seg = np.searchsorted(st, idx, side="right") - 1
    mask = idx < tend[seg]
    tc += np.bincount(seg[mask], weights=c["cost"][mask].astype(np.float64), minlength=ns)
    ttf += np.bincount(seg[mask], weights=c["cost_tf"][mask].astype(np.float64), minlength=ns)
np.savez("build/exp/X3c/fields.npz", title_cost=tc, title_tf=ttf, title_end=tend)
redir = np.zeros(ns, dtype=bool); k = 1
for line in open("build/exp/X2/meta_pages.tsv").readlines()[1:]:
    fl = line.rstrip("\n").split("\t")
    if fl[2] == "None":
        continue
    redir[k] = fl[7] == "1"; k += 1
cost = z["cost"]; ctf = z["cost_tf"]; ln = z["length"]
P = slice(1, ns - 2)  # main pages except the last one (its segment holds the tail blocks)
A = np.zeros(ns, dtype=bool); A[P] = True
for name, sel in (("articles", A & ~redir), ("redirect pages", A & redir)):
    print(f"{name}: {sel.sum():,} pages, coded {ln[sel].sum():,} B, final {cost[sel].sum() / 8:,.0f} B ({cost[sel].sum() / sel.sum():.1f} bits/page), "
          f"tf-alone {ctf[sel].sum() / 8:,.0f} B; title lines {tc[sel].sum() / 8:,.0f} B ({tc[sel].sum() / sel.sum():.1f} bits/title, tf-alone {ttf[sel].sum() / sel.sum():.1f})")
print("\ntext region by decile of coded position: bits/coded byte final, tf-alone, G%")
w = {k: z["w_" + k] for k in ("cost", "cost_tf", "cost_ppmd")}
nw = 554_867_444 >> 20
for d in range(10):
    a, b = d * nw // 10, (d + 1) * nw // 10
    c = w["cost"][a:b].sum(); t = w["cost_tf"][a:b].sum(); p = w["cost_ppmd"][a:b].sum(); L = (b - a) << 20
    print(f"{d}\t{c / L:.4f}\t{t / L:.4f}\t{100 * (t - c) / c:.2f}\tppmd {p / L:.4f}")
