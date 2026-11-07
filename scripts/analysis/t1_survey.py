#!/usr/bin/env python3
"""T1 survey: in-article "field-copy / derivation" laws (slots derivable from the title or earlier fields).

Usage: python3 -P -E scripts/analysis/t1_survey.py --pre X.pre [--cost COSTLOG]
  cost: the per-text-position cumulative cost cache build/exp/X2/tcost_<basename>.npy (x2_census.py makes it).

For every law: instances (the slot exists), holds (the law gives the right string), bytes the law fixes, and,
with --cost, the record's bits on exactly those bytes when the law holds (stake = an UPPER BOUND on the gain).
Text is phda9's (`[[x]]` -> `[x]`, `{{x}}` -> `{x}`). Rambot census pages are flagged and counted separately.
"""
import argparse, os, re, sys, unicodedata
from collections import defaultdict
import numpy as np

CENSUS = b"As of the [census|census]"


def strip_dia(s):
    return "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c))


def noparen(t):
    return re.sub(rb" \([^)]*\)$", b"", t)


def lastfirst(t):
    t = noparen(t)
    w = t.split(b" ")
    if len(w) < 2:
        return None
    suf = b""
    if w[-1] in (b"Jr.", b"Sr.", b"II", b"III", b"IV") and len(w) >= 3:
        suf = b", " + w[-1]; w = w[:-1]
        if w[-1].endswith(b","):
            w[-1] = w[-1][:-1]
    return w[-1] + b", " + b" ".join(w[:-1]) + suf


def pages(d):
    last = d.rfind(b"  </page>\n")
    st = [m.start() for m in re.finditer(rb"  <page>\n", d[:last])] + [last + 10]
    for i in range(len(st) - 1):
        s, e = st[i], st[i + 1]
        m = re.compile(rb"<title>(.*?)</title>").search(d, s, e)
        tx = d.find(b'<text xml:space="preserve">', s, e)
        if not m or tx < 0:
            continue
        yield m.group(1), tx + 27, e


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True); ap.add_argument("--cost")
    a = ap.parse_args()
    d = open(a.pre, "rb").read()
    cc = None
    if a.cost:
        cc = np.load(os.path.join("build/exp/X2", "tcost_" + os.path.basename(a.cost) + ".npy"))
        lim = len(cc) - 2
    else:
        lim = len(d)
    R = defaultdict(lambda: [0, 0, 0, 0.0, 0.0])  # instances, holds, bytes, bits on holds, bits on all instances

    def add(law, s, e, holds):
        if e > lim:
            return
        A = R[law]; A[0] += 1
        b = (cc[e] - cc[s]) if cc is not None else 0.0
        A[4] += b
        if holds:
            A[1] += 1; A[2] += e - s; A[3] += b

    re_cat = re.compile(rb"\[Category:([^\]|\n]*)(?:\|([^\]\n]*))?\]")
    re_bold = re.compile(rb"'''(.+?)'''")
    re_red = re.compile(rb"#REDIRECT ?\[([^\]\n|]+)", re.I)
    re_year = re.compile(rb"\b(\d{3,4})\b")
    re_name = re.compile(rb"\n *\| *name *= *([^\n|]*)")
    for t, s, e in pages(d):
        if s >= lim:
            break
        body = d[s:e]
        cen = CENSUS in body
        tag = "census:" if cen else ""
        # redirect target
        m = re_red.match(body)
        if m:
            x = m.group(1)
            ts = t.decode("utf8", "replace"); xs = x.decode("utf8", "replace")
            norm = lambda z: re.sub(r"[ _]", "", z).lower()
            add("redirect=title(case/space)", s + m.start(1), s + m.end(1) + 1, norm(xs) == norm(ts))
            add("redirect startswith title word", s + m.start(1), s + m.end(1) + 1,
                xs.split(" ")[0].lower() == ts.split(" ")[0].lower() and norm(xs) != norm(ts))
            continue
        # bold restatement in the first 600 bytes
        m = re_bold.search(body, 0, 600)
        if m:
            x = m.group(1)
            add(tag + "bold1=title", s + m.start(1), s + m.end(1) + 3, x == t)
            add(tag + "bold1=noparen(title)", s + m.start(1), s + m.end(1) + 3, x == noparen(t) and x != t)
        # infobox name = title
        m = re_name.search(body, 0, 3000)
        if m:
            x = m.group(1).strip()
            add(tag + "infobox name=title", s + m.start(1), s + m.end(1), x in (t, noparen(t)))
        # categories: first sort key; births/deaths years
        first_key = True
        lead = body[:1500]
        lead_years = set(int(y) for y in re_year.findall(lead))
        for m in re_cat.finditer(body):
            name, key = m.group(1), m.group(2)
            if key is not None and first_key:
                first_key = False
                lf = lastfirst(t)
                ks, end = s + m.start(2), s + m.end(2) + 1
                add(tag + "sortkey1=Last, First", ks, end, lf is not None and key == lf)
                add(tag + "sortkey1=title", ks, end, key in (t, noparen(t)))
                add(tag + "sortkey1=title minus The/A", ks, end,
                    re.sub(rb"^(The|A|An) ", b"", noparen(t)) == key and key not in (t, noparen(t)))
            elif key is not None:
                add(tag + "sortkey later (copy of earlier)", s + m.start(2), s + m.end(2) + 1, True)
            mb = re.match(rb"(\d{1,4})( BC)? (births|deaths)$", name)
            if mb:
                y = int(mb.group(1))
                add(tag + "cat " + mb.group(3).decode() + " year in lead", s + m.start(1), s + m.start(1) + len(mb.group(1)),
                    y in lead_years)
    print(f"{'law':42s} {'inst':>8s} {'holds':>8s} {'bytes':>9s} {'rec B(holds)':>12s} {'bits/hold':>9s} {'rec B(all)':>10s}")
    for k in sorted(R):
        n, h, b, c, call = R[k]
        print(f"{k:42s} {n:8,d} {h:8,d} {b:9,d} {c / 8:12,.0f} {c / max(h, 1):9.2f} {call / 8:10,.0f}")


main()
