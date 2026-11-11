#!/usr/bin/env python3
"""X3c: compare two real-stack cost logs of the SAME page set coded in two different orders (x3c_bench.sh samples).

  python3 -P -E scripts/analysis/x3c_pair.py NAME_A NAME_B      (names under build/exp/X3c/{cost,samples})
Prints, for A and B: bits by region (article text, metadata block and its page-id delta lines, interlanguage block,
rest) under the final model, the transformer alone, PPMD alone; then per-article deltas (B - A): totals, and the
regression of the final-cost delta and of the transformer-alone delta on the PPMD-alone delta (slope beta,
correlation r). beta is what turns a full-scale PPMD-only cost change into an estimate for the stack.
"""
import re
import sys

import numpy as np

PAT = b"  L\xdf\x98N\n    L\xdf\x9bN"
PEND = b"  L/\xdf\x98N\n"; CEND = b"/\xdf\x8fN\n"; TEND = b"L/\xdf\xa7N\n"
DT = np.dtype([("byte", "u1"), ("x", "u1", 7), ("pbit", "<u2", 8), ("cost", "<f4"), ("cost_tf", "<f4"),
               ("cost_fx", "<f4"), ("cost_ppmd", "<f4")])
KEYS = ("cost", "cost_tf", "cost_ppmd")
X = "build/exp/X3c/"


def load(name):
    r = np.fromfile(X + f"cost/{name}.bin", dtype=DT, offset=16)
    b = r["byte"].tobytes()
    pages = [int(x) for x in open(X + f"samples/{name}.xml.pages").read().split()]
    st = []
    i = b.find(PAT)
    while i >= 0:
        st.append(i); i = b.find(PAT, i + 1)
    assert len(st) == len(pages), (len(st), len(pages))
    meta0 = b.find(PEND, st[-1]) + len(PEND)
    meta1 = b.rfind(CEND) + len(CEND)
    il1 = b.rfind(TEND, meta1, b.find(b" xmlnsM", meta1)) + len(TEND)
    cs = {k: np.concatenate([[0.0], np.cumsum(r[k].astype(np.float64))]) for k in KEYS}
    bounds = st + [meta0]
    per = {pages[i]: {k: cs[k][bounds[i + 1]] - cs[k][bounds[i]] for k in KEYS} | {"len": bounds[i + 1] - bounds[i], "pos": bounds[i]}
           for i in range(len(pages))}
    reg = {"text": (0, meta0), "meta": (meta0, meta1), "interlang": (meta1, il1), "rest": (il1, len(r))}
    R = {n: {k: cs[k][e] - cs[k][s] for k in KEYS} | {"len": e - s} for n, (s, e) in reg.items()}
    # page-id delta lines of the metadata block
    pid = 0.0; pos = meta0; nrec = 0
    for line in b[meta0:meta1].split(b"\n")[:-1]:
        e = pos + len(line) + 1
        if re.fullmatch(rb"N-?\d+", line):
            pid += cs["cost"][e] - cs["cost"][pos]; nrec += 1
        pos = e
    R["meta: page-id delta lines"] = {"cost": pid, "cost_tf": 0, "cost_ppmd": 0, "len": nrec}
    R["total"] = {k: cs[k][-1] for k in KEYS} | {"len": len(r)}
    return per, R


def main():
    a, b = sys.argv[1], sys.argv[2]
    pa, ra = load(a); pb, rb = load(b)
    print(f"A = {a}, B = {b}; bytes (bits/8)")
    print("region\tcoded B\tfinal A\tfinal B\tB-A\ttf-alone A\ttf-alone B\tB-A\tppmd A\tppmd B\tB-A")
    for n in ra:
        x, y = ra[n], rb[n]
        print(f"{n}\t{x['len']:,}\t" + "\t".join(f"{x[k] / 8:,.0f}\t{y[k] / 8:,.0f}\t{(y[k] - x[k]) / 8:+,.0f}" for k in KEYS))
    common = [p for p in pa if p in pb]
    assert len(common) == len(pa) and all(pa[p]["len"] == pb[p]["len"] for p in common[:-1])
    d = {k: np.array([pb[p][k] - pa[p][k] for p in common]) / 8 for k in KEYS}
    L = np.array([pa[p]["len"] for p in common])
    print(f"\nper-article deltas B-A over {len(common)} articles: final {d['cost'].sum():+,.0f} B (sum |d| {np.abs(d['cost']).sum():,.0f}), "
          f"tf-alone {d['cost_tf'].sum():+,.0f} B, ppmd-alone {d['cost_ppmd'].sum():+,.0f} B (sum |d| {np.abs(d['cost_ppmd']).sum():,.0f})")
    for k in ("cost", "cost_tf"):
        x = d["cost_ppmd"]; y = d[k]
        beta = (x * y).sum() / (x * x).sum(); r = np.corrcoef(x, y)[0, 1]
        print(f"  d({k}) = beta * d(ppmd): beta {beta:.3f} (through origin), r {r:.3f}; ratio of sums {y.sum() / x.sum():.3f}")
    np.savez(X + f"pair_{a}__{b}.npz", pages=np.array(common), dcost=d["cost"], dtf=d["cost_tf"], dppmd=d["cost_ppmd"], length=L)


if __name__ == "__main__":
    main()
