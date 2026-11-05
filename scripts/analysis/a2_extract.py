#!/usr/bin/env python3
"""A2 extraction: wiki link targets and article titles of a slice, as ranges of the CODED (WRT) stream.

Usage:
  a2_extract.py --cost COSTLOG --pre SLICE.PRE --dict english.dic --out SPANS.tsv

Writes one line per span, in coded-stream order:
  kind  cs  ce  article  a1class  text
    kind     L = internal link target, T = article title, A = anchor text of the preceding piped link L
    cs, ce   coded byte range [cs, ce). For L it starts after the opening '[' (phda9 notation: '[x]' is the
             original '[[x]]') and INCLUDES the terminator ('|' or ']'). For T it is the title text only. For A it
             starts after '|' and includes the closing ']' (anchors that contain a nested '[' are skipped).
    article  index of the <page> the span lies in (0-based)
    a1class  1 if the first target byte has A1's class "link target" (links inside templates, refs, tables,
             infoboxes and redirect pages are painted over by those classes in A1's table), else 0
    text     the decoded target / title text (tabs and newlines escaped), for statistics only

The decoder and classifier are scripts/analysis/a1_analyze.py's (WRT decode verified against slice.pre).
Category:, Image:, File: and Media: links are excluded, as in A1.
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


def link_spans(art, text_spans):
    """(a, tend, close) for every internal link: a = position of '[', tend = position of its terminator ('|' or ']'),
    close = position of the closing ']' (== tend for an unpiped link)."""
    n = len(art)
    in_text = np.zeros(n, bool)
    for a, b in text_spans:
        in_text[a:b] = True
    out = []
    stack = []
    for m in re.finditer(rb"\[\[|\]\]|\[|\]|\n\n", art):
        tok = m.group()
        if tok in (b"\n\n", b"[[", b"]]"):
            if tok == b"\n\n":
                stack.clear()
            continue
        if tok == b"[":
            stack.append(m.start())
        elif stack:
            a = stack.pop()
            b = m.end()
            if not in_text[a]:
                continue
            body = art[a + 1:b - 1]
            if body.startswith((b"Category:", b"category:", b"Image:", b"image:", b"File:", b"Media:")):
                continue
            bar = body.find(b"|")
            tend = b - 1 if bar < 0 else a + 1 + bar
            if b"[" in art[a + 1:tend]:  # nested link inside the target part: not a plain target
                continue
            out.append((a, tend, b - 1))
    out.sort()
    return out


def esc(b):
    return b.decode("latin-1").replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cost", required=True)
    ap.add_argument("--pre", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    r = costlog.load(args.cost, mmap=False)
    stream = r["byte"].tobytes()
    text, cmap, container = a1.wrt_decode(stream, a1.load_dict(args.dict))
    if text != open(args.pre, "rb").read():
        raise SystemExit("WRT decode mismatch")
    tcls, sections = a1.classify(text)
    bcls = tcls[cmap]
    art_end = sections["articles"][1]
    art = text[:art_end]
    text_spans = [(m.start(1), m.end(1)) for m in re.finditer(rb'<text xml:space="preserve">(.*?)</text>', art, re.S)]
    pages = np.array([m.start() for m in re.finditer(rb"  <page>\n", art)], np.int64)

    def crange(t0, t1):
        """coded range of text positions [t0, t1)"""
        return int(np.searchsorted(cmap, t0, "left")), int(np.searchsorted(cmap, t1, "left"))

    rows = []
    for m in re.finditer(rb"<title>(.*?)</title>", art):
        cs, ce = crange(m.start(1), m.end(1))
        rows.append((cs, "T", ce, int(np.searchsorted(pages, m.start(), "right") - 1), 0, m.group(1)))
    nl = 0
    for a, tend, close in link_spans(art, text_spans):
        cs, ce = crange(a + 1, tend + 1)
        if ce <= cs:
            continue
        # sanity: the coded range must decode exactly to the target + terminator
        assert cmap[cs] == a + 1 and (ce == len(cmap) or cmap[ce] > tend), (a, tend)
        a1c = int(bcls[cs] == a1.C["link target"])
        rows.append((cs, "L", ce, int(np.searchsorted(pages, a, "right") - 1), a1c, text[a + 1:tend + 1]))
        nl += 1
        if close > tend and b"[" not in art[tend + 1:close]:  # anchor text of a piped link, with the closing ']'
            ca, cb = crange(tend + 1, close + 1)
            if cb > ca:
                rows.append((ca, "A", cb, int(np.searchsorted(pages, a, "right") - 1), a1c, text[tend + 1:close + 1]))
    rows.sort(key=lambda x: (x[0], x[1]))
    with open(args.out, "w") as f:
        for cs, kind, ce, page, a1c, t in rows:
            f.write(f"{kind}\t{cs}\t{ce}\t{page}\t{a1c}\t{esc(t)}\n")
    lt = bcls == a1.C["link target"]
    cost = r["cost"].astype(np.float64)
    print(f"{len(stream):,} coded bytes; {len(pages):,} pages; {nl:,} links; A1 'link target' class: "
          f"{int(lt.sum()):,} coded bytes, {cost[lt].sum() / 8:,.0f} B of {cost.sum() / 8:,.0f} B")


if __name__ == "__main__":
    main()
