#!/usr/bin/env python3
"""T1: per-language title-link counts of the full run's interwiki groups before reordered rank R (t1_csim seed=).

Usage: python3 -P -E scripts/analysis/t1_seed.py --rank R --meta build/exp/X2/meta_pages.tsv --out SEED.txt
Needs build/exp/T1/full_groups.pkl (title -> full-run group, made by build/exp/T1/check_proxy.py) and full.pre for
the page-index -> title map. Rows: lang eq n (eq = links whose title is the page title).
"""
import argparse, pickle, re
from collections import defaultdict

RE_LINK = re.compile(rb"\[([a-z][a-z\-]{1,11}):([^\]\n]*)\](?=\n|</text>)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rank", type=int, required=True); ap.add_argument("--meta", required=True)
    ap.add_argument("--out", required=True); ap.add_argument("--pre", default="build/exp/A1/full/full.pre")
    ap.add_argument("--ref", default="build/exp/T1/full_groups.pkl")
    a = ap.parse_args()
    ref = pickle.load(open(a.ref, "rb"))
    d = open(a.pre, "rb").read()
    titles = [m.group(1) for m in re.finditer(rb"  <page>\n    <title>(.*?)</title>", d)]
    del d
    R = defaultdict(lambda: [0, 0])
    with open(a.meta) as f:
        next(f)
        for line in f:
            rank, page = (int(x) for x in line.split("\t")[:2])
            if rank >= a.rank:
                continue
            t = titles[page]
            g = ref.get(t)
            if g is None:
                continue
            for m in RE_LINK.finditer(g + b"</text>"):
                r = R[m.group(1).decode()]
                r[0] += m.group(2) == t; r[1] += 1
    with open(a.out, "w") as f:
        for k, (e, n) in sorted(R.items()):
            f.write(f"{k} {e} {n}\n")
    print(f"{len(R)} languages, {sum(v[1] for v in R.values()):,} links before rank {a.rank}")


main()
