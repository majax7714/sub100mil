#!/usr/bin/env python3
"""X2: what the record pays on the interlanguage-link block (phda9 moves it after the last page).

Usage: python3 -P -E scripts/analysis/x2_interwiki.py --pre SLICE.PRE --cost COSTLOG --dict english.dic
Splits link targets by language, by script, and by whether the same target string (or a near copy) already
appeared earlier in the same article's group. Groups: a new group starts when the language code sorts before
the previous one (the block is sorted by code within an article).
"""
import argparse, os, re, sys
from collections import defaultdict
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x2_census as x  # noqa: E402


def lev(a, b):
    if len(a) < len(b):
        a, b = b, a
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a):
        cur = [i + 1]
        for j, cb in enumerate(b):
            cur.append(min(prev[j + 1] + 1, cur[j] + 1, prev[j] + (ca != cb)))
        prev = cur
    return prev[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True); ap.add_argument("--cost", required=True); ap.add_argument("--dict")
    a = ap.parse_args()
    text = open(a.pre, "rb").read()
    ccum = np.load(os.path.join("build/exp/X2", "tcost_" + os.path.basename(a.cost) + ".npy"))
    st = x.pages_of(text)
    tail0 = st[-1]
    tail = text[tail0:]
    links = [(m.group(1), m.group(2), tail0 + m.start(2), tail0 + m.end(2)) for m in x.RE_IW.finditer(tail)]
    tot = ccum[-1]
    by = defaultdict(lambda: [0, 0, 0.0])
    groups = []
    prev = None
    for l in links:
        if prev is None or l[0] < prev:
            groups.append([])
        groups[-1].append(l)
        prev = l[0]
    allb = 0.0
    for g in groups:
        seen = []
        for lang, t, s, e in g:
            b = ccum[e] - ccum[s] + (ccum[s] - ccum[s - len(lang) - 2])  # target + "[lang:" prefix
            allb += b
            sc = x.iw_script(t)
            if t in [u for _, u in seen]:
                rel = "copy"
            elif seen and min(lev(t.decode('utf8', 'replace'), u.decode('utf8', 'replace')) for _, u in seen) <= max(2, len(t.decode('utf8','replace')) // 5):
                rel = "near"
            else:
                rel = "new"
            for k in (("script", sc), ("rel", rel), ("script+rel", sc + "/" + rel), ("lang", lang.decode())):
                A = by[k]; A[0] += 1; A[1] += len(t); A[2] += b
            seen.append((lang, t))
    print(f"groups {len(groups):,}, links {len(links):,}, bits on links {allb / 8:,.0f} B of {tot / 8:,.0f} B ({100 * allb / tot:.2f}%)")
    for kind in ("script", "rel", "script+rel", "lang"):
        rows = sorted(((k[1], v) for k, v in by.items() if k[0] == kind), key=lambda r: -r[1][2])
        print(f"-- by {kind}")
        for name, (n, ch, b) in rows[:25]:
            print(f"  {name:22s} {n:7d} links {ch:8d} B text {b / 8:9,.0f} B  {b / n:6.1f} bits/link {b / ch:5.2f} bits/B")


main()
