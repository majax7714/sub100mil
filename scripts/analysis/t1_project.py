#!/usr/bin/env python3
"""T1: full-enwik9 projection of the interlanguage field-copy gain from reordered interwiki proxies [ESTIMATE].

Usage: python3 -P -E scripts/analysis/t1_project.py --bins build/exp/T1/rank_bins.tsv \
           --point build/exp/T1/iwp_5k.pre:build/exp/T1/csim_warm_5k.txt ...
Each point: a proxy (t1_proxy.py) and t1_csim's output on its cost log. The proxy's median full-run rank is found
from its titles (meta_pages.tsv + full.pre). Gain per normaliser (title links "eqT", groups, eqT+lcpT4 links) is
interpolated linearly in rank between points (held constant outside) and summed over the 10K-rank bins of
rank_bins.tsv (full-run counts per bin).
"""
import argparse, re, statistics, sys
sys.path.insert(0, "scripts/analysis")
src = open("scripts/analysis/t1_iw.py").read().replace("\nmain()\n", "\n"); ns = {}; exec(compile(src, "t1iw", "exec"), ns)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bins", required=True); ap.add_argument("--point", action="append", required=True)
    ap.add_argument("--meta", default="build/exp/X2/meta_pages.tsv"); ap.add_argument("--pre", default="build/exp/A1/full/full.pre")
    a = ap.parse_args()
    d = open(a.pre, "rb").read()
    titles = [m.group(1) for m in re.finditer(rb"  <page>\n    <title>(.*?)</title>", d)]
    del d
    rank_of = {}
    for line in open(a.meta).readlines()[1:]:
        r, p = (int(x) for x in line.split("\t")[:2])
        rank_of[titles[p]] = r
    pts = []
    for spec in a.point:
        pre, sim = spec.split(":")
        x = open(pre, "rb").read()
        tl, gs = ns["parse"](x)
        cnt = {"eqT": 0, "lcpT4": 0}
        for t, (s, e) in zip(tl, gs):
            seen = []
            for m in ns["RE_LINK"].finditer(x, s, e + 7):
                r = ns["rel"](m.group(2), t, seen); seen.append(m.group(2))
                if r in cnt: cnt[r] += 1
        g = float(re.search(r"\(([+-]\d+) B\)", open(sim).read()).group(1))
        mid = statistics.median(rank_of[t] for t in tl if t in rank_of)
        pts.append((mid, -g, cnt["eqT"], len(gs), cnt["eqT"] + cnt["lcpT4"]))
        print(f"point {pre}: median rank {mid:,.0f}, gain {-g:,.0f} B, eqT {cnt['eqT']:,}, groups {len(gs):,}; "
              f"{-g / cnt['eqT']:.3f} B/eqT, {-g / len(gs):.3f} B/group")
    pts.sort()
    rows = [l.rstrip("\n").split("\t") for l in open(a.bins)][1:]
    hdr = open(a.bins).readline().rstrip("\n").split("\t")
    col = {h: i for i, h in enumerate(hdr)}

    def interp(r, k):
        ys = [(p[0], p[1] / p[k]) for p in pts]
        if r <= ys[0][0]: return ys[0][1]
        if r >= ys[-1][0]: return ys[-1][1]
        for (x0, y0), (x1, y1) in zip(ys, ys[1:]):
            if x0 <= r <= x1: return y0 + (y1 - y0) * (r - x0) / (x1 - x0)
    for k, name, cols in ((2, "per title link (eqT)", ["eqT"]), (3, "per group", ["groups"]),
                          (4, "per eqT+lcpT4 link", ["eqT", "lcpT4"])):
        tot = 0.0
        for row in rows:
            r = int(row[0]) + 5000
            n = sum(int(row[col[c]]) for c in cols)
            tot += n * interp(r, k)
        print(f"projection {name}: {tot / 1000:,.1f} KB")


main()
