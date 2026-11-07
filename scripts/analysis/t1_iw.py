#!/usr/bin/env python3
"""T1: interlanguage-link titles vs the English page title (phda9's interwiki block, after the metadata block).

Usage: python3 -P -E scripts/analysis/t1_iw.py --pre X.pre [--cost COSTLOG --dict english.dic]
  (cost: the per-text-position cache build/exp/{X2,T1}/tcost_<name>.npy, computed into build/exp/T1 if missing)

Decoder-valid mapping: phda9 moves an article's trailing interlanguage links plus its "</text>" into one group of
the block; the article keeps no "</text>". So the k-th article without "</text>" owns the k-th group (groups end
with "</text>\n"), and every title is decoded before the block. Per link the relation of its title X to the
English title T and to earlier links of the same group is classified, and the record's bits are summed on the
title bytes plus the closing "]".
"""
import argparse, os, re, sys, unicodedata
from collections import defaultdict
import numpy as np

RE_LINK = re.compile(rb"\[([a-z][a-z\-]{1,11}):([^\]\n]*)\](?=\n|</text>)")


def strip_dia(s):
    return "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c))


def tcost(cost, dic, text):
    """Cumulative record cost per text position (x2_census.py's cache; T1's own caches go to build/exp/T1)."""
    name = "tcost_" + os.path.basename(cost) + ".npy"
    for dd in ("build/exp/X2", "build/exp/T1"):
        if os.path.exists(os.path.join(dd, name)):
            return np.load(os.path.join(dd, name))
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import a1_analyze as a1  # noqa: E402
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
    import costlog  # noqa: E402
    r = costlog.load(cost, mmap=False)
    dec, cmap, container = a1.wrt_decode(r["byte"].tobytes(), a1.load_dict(dic))
    if text[:len(dec)] != dec:
        raise SystemExit("WRT decode mismatch")
    tc = np.zeros(len(dec) + 1, np.float64)
    np.add.at(tc, np.minimum(cmap, len(dec)), r["cost"].astype(np.float64))
    cc = np.concatenate([[0.0], np.cumsum(tc)])
    np.save(os.path.join("build/exp/T1", name), cc)
    return cc


def parse(d):
    last = d.rfind(b"</page>\n")
    starts = [m.start() for m in re.finditer(rb"  <page>\n", d[:last])]
    titles = []
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else last
        p = d[s:e]
        if b"</text>" not in p and b'<text xml:space="preserve" />' not in p:
            titles.append(re.search(rb"<title>(.*?)</title>", p).group(1))
    s = d.find(b"/contributor>\n[", last) + 14
    e = d.rfind(b"</text>\n") + 8
    gs = []
    pos = s
    while pos < e:
        q = d.find(b"</text>\n", pos)
        gs.append((pos, q))
        pos = q + 8
    return titles, gs


def rel(x, t, seen):
    if x in seen:
        return "copy"
    xs, ts = x.decode("utf8", "replace"), t.decode("utf8", "replace")
    if x == t:
        return "eqT"
    if xs[:1].upper() + xs[1:] == ts or xs.replace("_", " ") == ts:
        return "eqT_case"
    if strip_dia(xs) == strip_dia(ts):
        return "eqT_dia"
    tp = re.sub(r" \([^)]*\)$", "", ts)
    if tp != ts and xs == tp:
        return "eqT_noparen"
    for u in seen:
        us = u.decode("utf8", "replace")
        if strip_dia(xs) == strip_dia(us):
            return "near_dia"
    # common prefix with T in bytes
    n = 0
    while n < min(len(x), len(t)) and x[n] == t[n]:
        n += 1
    if n >= 4:
        return "lcpT4"
    return "other"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True); ap.add_argument("--cost"); ap.add_argument("--dict")
    a = ap.parse_args()
    d = open(a.pre, "rb").read()
    titles, gs = parse(d)
    print(f"articles without </text>: {len(titles):,}; groups: {len(gs):,}")
    if len(gs) == len(titles) + 1:  # partial first article of a raw slice (mid10M): its group has no title
        gs = gs[1:]
    assert len(titles) == len(gs), "mapping broken"
    cc = None
    if a.cost:
        cc = tcost(a.cost, a.dict, d)
    by = defaultdict(lambda: [0, 0, 0.0, 0.0])  # links, title bytes, bits on title+"]", bits on lcp-with-T prefix
    for t, (s, e) in zip(titles, gs):
        seen = []
        for m in RE_LINK.finditer(d, s, e + 7):
            x = m.group(2)
            r = rel(x, t, seen)
            A = by[r]; A[0] += 1; A[1] += len(x)
            if cc is not None and m.end(2) + 1 < len(cc):
                A[2] += cc[m.end(2) + 1] - cc[m.start(2)]
                n = 0
                while n < min(len(x), len(t)) and x[n] == t[n]:
                    n += 1
                A[3] += cc[m.start(2) + n] - cc[m.start(2)]
            seen.append(x)
    tot = [0, 0, 0.0]
    print(f"{'class':12s} {'links':>9s} {'bytes':>10s} {'rec B':>9s} {'bits/link':>9s} {'bits/B':>6s} {'lcpT B':>8s}")
    for r, (n, b, c, l) in sorted(by.items(), key=lambda kv: -kv[1][1]):
        print(f"{r:12s} {n:9,d} {b:10,d} {c / 8:9,.0f} {c / max(n, 1):9.2f} {c / max(b, 1):6.2f} {l / 8:8,.0f}")
        tot[0] += n; tot[1] += b; tot[2] += c
    print(f"{'all':12s} {tot[0]:9,d} {tot[1]:10,d} {tot[2] / 8:9,.0f}")


main()
