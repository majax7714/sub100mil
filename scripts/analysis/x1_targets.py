#!/usr/bin/env python3
"""X1 target census: what the record still pays on the byte regions each cross-domain mechanism would act on.

Usage:
  python3 -I scripts/analysis/x1_targets.py --cost COSTLOG --pre SLICE.PRE --dict english.dic [--keys OUT.tsv]

For every coded byte we know the text position it produces (A1's verified WRT decoder). A region is a set of text
spans; its cost is the sum of the record's bits over the coded bytes that land in those spans. Regions:
  sortkey.*     category sort keys `[Category:X|KEY]` (DNA inverted-repeat analogue: KEY is often the title
                with its words permuted, "Warhol, Andy" for <title>Andy Warhol</title>). Split by first key in the
                article vs later keys, and by KEY's relation to the title.
  close.*       closing delimiters (conservation / nesting): `]`, `}}`, `|}`, `</ref>`, closing `''`/`'''`.
  heading       section heading text (multiscale / renormalization: the coarse state).
  table.sep     table row/cell separators at line starts inside `{| |}` (crystal periodicity).
  newline.para  newlines that end a prose paragraph (segment-length hazard).
  date          month-day-year dates (article "dialect"; codon-usage analogue).
  digits        all digits (reference only; E12 covers numeric consistency).
  offset.*      coded bytes by offset from the page start (coded-byte buckets; transformer window is 1024 tokens).
"""
import argparse
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a1_analyze as a1  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
import costlog  # noqa: E402


def norm_words(s):
    return [w for w in re.split(rb"[^0-9A-Za-z\x80-\xff]+", s.lower()) if w]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cost", required=True)
    ap.add_argument("--pre", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--keys", help="write per-sort-key rows (title, key, kind, first, bits) here")
    args = ap.parse_args()

    r = costlog.load(args.cost, mmap=False)
    stream = r["byte"].tobytes()
    cost = r["cost"].astype(np.float64)
    text, cmap, container = a1.wrt_decode(stream, a1.load_dict(args.dict))
    if text != open(args.pre, "rb").read():
        raise SystemExit("WRT decode mismatch")
    n = len(text)
    # cost per text char: coded byte cost lands on the char it maps to
    tcost = np.zeros(n + 1, np.float64)
    np.add.at(tcost, np.minimum(cmap, n), cost)
    ccount = np.zeros(n + 1, np.int64)
    np.add.at(ccount, np.minimum(cmap, n), 1)
    ctc = np.concatenate([[0.0], np.cumsum(tcost)])
    ccc = np.concatenate([[0], np.cumsum(ccount)])
    total = cost.sum()

    def span_sum(sp):
        b = sum(ctc[e] - ctc[s] for s, e in sp)
        c = sum(ccc[e] - ccc[s] for s, e in sp)
        return b, c

    rows = []

    def region(name, sp, note=""):
        b, c = span_sum(sp)
        rows.append((name, len(sp), c, b / 8, b / max(c, 1), 100 * b / total, note))

    art_end = text.rfind(b"  </page>\n") + len(b"  </page>\n")
    art = text[:art_end]
    pages = [m.start() for m in re.finditer(rb"  <page>\n", art)] + [art_end]

    # ---- sort keys
    kinds = {}
    keyrows = []
    for pi in range(len(pages) - 1):
        a, b = pages[pi], pages[pi + 1]
        pg = art[a:b]
        tm = re.search(rb"<title>(.*?)</title>", pg)
        title = tm.group(1) if tm else b""
        tw = norm_words(title)
        first = True
        seen_keys = set()
        for m in re.finditer(rb"\[Category:[^\]|\n]*\|([^\]\n]*)\]", pg):
            key = m.group(1)
            kw = norm_words(key)
            if key.strip() in (b"", b"*", b" "):
                kind = "trivial"
            elif key.lower() == title.lower():
                kind = "=title"
            elif kw and sorted(kw) == sorted(tw):
                kind = "perm(title)"
            elif kw and set(kw) <= set(tw):
                kind = "subset(title)"
            elif kw and len(set(kw) & set(tw)) > 0:
                kind = "overlap(title)"
            else:
                kind = "other"
            rep = key in seen_keys
            seen_keys.add(key)
            tag = ("first" if first else ("repeat" if rep else "later-new"))
            first = False
            s, e = a + m.start(1), a + m.end(1) + 1  # include the closing ']' (key end is also predicted)
            kinds.setdefault((tag, kind), []).append((s, e))
            if args.keys:
                bb, _ = span_sum([(s, e)])
                keyrows.append((pi, title.decode("utf8", "replace"), key.decode("utf8", "replace"), kind, tag, bb))
    for (tag, kind), sp in sorted(kinds.items()):
        region(f"sortkey.{tag}.{kind}", sp)
    if args.keys:
        with open(args.keys, "w") as f:
            f.write("page\ttitle\tkey\tkind\ttag\tbits\n")
            for row in keyrows:
                f.write("\t".join(str(x) for x in row[:5]) + f"\t{row[5]:.2f}\n")

    # ---- closing delimiters (nesting / conservation)
    region("close.]", [(m.start(), m.end()) for m in re.finditer(rb"\]", art)])
    region("close.}}", [(m.start(), m.end()) for m in re.finditer(rb"\}\}", art)])
    region("close.</ref>", [(m.start(), m.end()) for m in re.finditer(rb"</ref>", art)])
    region("close.''", [(m.start(), m.end()) for m in re.finditer(rb"(?<=[^'\s])'{2,5}(?=[^'])", art)])
    # ---- headings
    region("heading", [(m.start(1), m.end(1)) for m in re.finditer(rb"^=+ *(.*?) *=+ *$", art, re.M)])
    # ---- tables
    tsp = []
    for m in re.finditer(rb"\{\|.*?\n\|\}", art, re.S):
        for l in re.finditer(rb"^(\|-|\|\||\||!)", m.group(0), re.M):
            tsp.append((m.start() + l.start(), m.start() + l.end()))
    region("table.sep", tsp)
    # ---- paragraph-ending newlines in prose
    region("newline.para", [(m.start(), m.start() + 1) for m in re.finditer(rb"(?<=[.!?\)\]'\"]) *\n\n", art)])
    # ---- dates
    mon = rb"(?:January|February|March|April|May|June|July|August|September|October|November|December)"
    region("date", [(m.start(), m.end()) for m in re.finditer(
        rb"\b" + mon + rb" \d{1,2},? \d{3,4}|\b\d{1,2} " + mon + rb",? \d{3,4}", art)])
    region("digits", [(m.start(), m.end()) for m in re.finditer(rb"\d+", art)])

    # ---- offsets from page start in coded bytes
    pstart_coded = np.searchsorted(cmap, np.array(pages[:-1]), "left")
    idx = np.arange(len(cost))
    pg = np.searchsorted(pstart_coded, idx, "right") - 1
    off = np.where(pg >= 0, idx - pstart_coded[np.maximum(pg, 0)], -1)
    in_art = (pg >= 0) & (cmap < art_end)
    edges = [0, 256, 1024, 2048, 4096, 8192, 16384, 32768, 1 << 40]
    for lo, hi in zip(edges[:-1], edges[1:]):
        sel = in_art & (off >= lo) & (off < hi)
        b = cost[sel].sum()
        c = int(sel.sum())
        rows.append((f"offset.[{lo},{hi})", "", c, b / 8, b / max(c, 1), 100 * b / total,
                     f"tf {r['cost_tf'][sel].sum() / max(c, 1):.3f} b/B"))

    print(f"total {total / 8:,.0f} B over {len(cost):,} coded bytes, {len(pages) - 1:,} pages\n")
    print(f"{'region':34s} {'spans':>8s} {'coded B':>10s} {'record B':>10s} {'bits/cB':>8s} {'% bits':>7s}  note")
    for name, ns, c, b, bpc, pct, note in rows:
        print(f"{name:34s} {str(ns):>8s} {c:>10,d} {b:>10,.0f} {bpc:>8.3f} {pct:>6.2f}%  {note}")


if __name__ == "__main__":
    main()
