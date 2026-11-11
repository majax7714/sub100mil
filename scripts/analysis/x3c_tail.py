#!/usr/bin/env python3
"""X3c: region split of the C3 full-run cost dump: article text, metadata block (per field), interlanguage block,
intro+coda. Needs build/exp/X3c/pages_full.npz (x3c_pages.py) and the full coded stream build/exp/E12/full/e9.stream.

Usage: python3 -P -E scripts/analysis/x3c_tail.py > build/exp/X3c/tail.txt
"""
import mmap
import re
import numpy as np

DT = np.dtype([("byte", "u1"), ("x", "u1", 7), ("pbit", "<u2", 8), ("cost", "<f4"), ("cost_tf", "<f4"),
               ("cost_fx", "<f4"), ("cost_ppmd", "<f4")])
z = np.load("build/exp/X3c/pages_full.npz")
st = z["starts"]; n = int(z["n"])
f = open("build/exp/E12/full/e9.stream", "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
r = np.memmap("build/exp/FULL/cost_full.bin", dtype=DT, mode="r", offset=16)
s = int(st[-2]); e = int(st[-1])
PEND = b"  L/\xdf\x98N\n"
meta0 = m.find(PEND, s) + len(PEND)
CEND = b"/\xdf\x8fN\n"
meta1 = m.rfind(CEND, s, e) + len(CEND)
TEND = b"L/\xdf\xa7N\n"
il1 = m.rfind(TEND, meta1, e) + len(TEND)
il1 = m.rfind(TEND, meta1, m.find(b" xmlnsM", meta1)) + len(TEND)
tot = {k: float(z[k].sum()) for k in ("cost", "cost_tf", "cost_fx", "cost_ppmd")}


def rng(a, b):
    c = np.array(r[a:b]); return {k: float(c[k].astype(np.float64).sum()) for k in ("cost", "cost_tf", "cost_fx", "cost_ppmd")}


regs = {"text (pages)": (0, meta0), "metadata block": (meta0, meta1), "interlanguage block": (meta1, il1), "intro+coda": (il1, n)}
print(f"boundaries: meta0 {meta0:,} meta1 {meta1:,} il1 {il1:,} n {n:,}")
print("region\tcoded B\tfinal B\ttf-alone B\tG = tf - final (B)\tG %\tppmd-alone B")
out = {}
for name, (a, b) in regs.items():
    if name == "text (pages)":
        t = rng(meta0 - (meta0 - s), meta0)  # last page only
        d = {k: tot[k] - sum(rng(s, n)[k] for _ in [0]) + t[k] for k in tot}
    else:
        d = rng(a, b)
    out[name] = d
    print(f"{name}\t{b - a:,}\t{d['cost'] / 8:,.0f}\t{d['cost_tf'] / 8:,.0f}\t{(d['cost_tf'] - d['cost']) / 8:,.0f}\t"
          f"{100 * (d['cost_tf'] - d['cost']) / d['cost']:.2f}\t{d['cost_ppmd'] / 8:,.0f}")
# metadata block per field
blk = m[meta0:meta1]
c = np.array(r[meta0:meta1]); cost = c["cost"].astype(np.float64); ctf = c["cost_tf"].astype(np.float64)
cs = np.concatenate([[0], np.cumsum(cost)]); ct = np.concatenate([[0], np.cumsum(ctf)])
fields = {}
rec = -1; prev = None; pid_cost = []
pos = 0
for line in blk.split(b"\n")[:-1]:
    a, b = pos, pos + len(line) + 1
    pos = b
    if rec < 0 and line.isdigit():
        k = "header"
    elif line[:1] == b"N" and re.fullmatch(rb"N-?\d+", line):
        k = "page-id delta"; rec += 1; pid_cost.append(cs[b] - cs[a])
    elif line.startswith(b"\xdf\x86N"):
        k = "revision id" if prev == b"\xdf\x99N" else "user id"
    elif line.startswith(b"\xdf\xcdN"):
        k = "timestamp"
    elif line.startswith(b"\xdf\x87N"):
        k = "username"
    elif line in (b"\xdf\x99N", b"\xdf\x8fN", b"/\xdf\x8fN"):
        k = "tags"
    else:
        k = "ip/other"
    d = fields.setdefault(k, [0, 0.0, 0.0, 0]); d[0] += 1; d[1] += cs[b] - cs[a]; d[2] += ct[b] - ct[a]; d[3] += b - a
    prev = line
print(f"\nmetadata block: {rec + 1:,} records")
print("field\tlines\tcoded B\tfinal B\tbits/line\ttf-alone bits/line")
for k, d in sorted(fields.items(), key=lambda x: -x[1][1]):
    print(f"{k}\t{d[0]:,}\t{d[3]:,}\t{d[1] / 8:,.0f}\t{d[1] / d[0]:.2f}\t{d[2] / d[0]:.2f}")
pc = np.array(pid_cost)
np.save("build/exp/X3c/pid_cost.npy", pc)
print("\npage-id delta bits by record rank (10 equal bins):", " ".join(f"{x.mean():.2f}" for x in np.array_split(pc, 10)))
print(f"page-id delta: articles (first 172,277 records) {pc[:172277].sum() / 8:,.0f} B = {pc[:172277].mean():.2f} bits; "
      f"rest {pc[172277:].sum() / 8:,.0f} B = {pc[172277:].mean():.2f} bits")
