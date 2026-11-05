#!/usr/bin/env python3
"""A2 full-scale statistics: how often a link target was seen before, on full enwik9, in identity order and in the
record's reordered article order (cmix -e: new_article_order, redirect pages appended at the end).

Usage:
  a2_fullscale.py FULL.PRE NEW_ARTICLE_ORDER [--slice-pages N]

FULL.PRE is `slicetool pre` of full enwik9 (identity order, phda9 notation: '[x]' is the original '[[x]]').
Text-level, approximate link parsing: '[' target ('|' or ']'), no nested '[' inside the target; Category:,
Image:, File:, Media: excluded (as in A1/A2). Keys are case-folded on the first letter, '_' -> ' '.

Prints, per order, the links and target bytes split by: seen earlier in the same article; seen earlier as a
link in another article (by earlier-occurrence count); seen only as an earlier title; title of one of the
previous K pages; never seen. `--slice-pages N` also prints the identity-order stats of the first N pages
(the 10M slice has 1,325 pages), to calibrate against the slice simulation.
"""
import collections
import re
import sys

LINK = re.compile(rb"(?<!\[)\[(?!\[)([^\[\]|\n]{1,255})[\]|]")
TITLE = re.compile(rb"<title>(.*?)</title>")
SKIP = (b"Category:", b"category:", b"Image:", b"image:", b"File:", b"Media:")
REDIR = [b'      <text xml:space="preserve">' + p for p in (b"#REDIRECT", b"#redirect", b"#Redirect", b"#REdirect",
                                                             b"{softredirect", b"{{softredirect")]


def key(t):
    t = t.replace(b"_", b" ").strip()
    return t[:1].upper() + t[1:]


def parse(path):
    data = open(path, "rb").read()
    pages = []
    pos = data.find(b"  <page>\n")
    while pos >= 0:
        end = data.find(b"  </page>\n", pos)
        if end < 0:
            break
        pg = data[pos:end]
        m = TITLE.search(pg)
        title = key(m.group(1)) if m else b""
        redirect = any(l in pg for l in REDIR)
        links = [key(x) for x in LINK.findall(pg) if not x.startswith(SKIP) and b"://" not in x]
        lens = [len(x) + 1 for x in LINK.findall(pg) if not x.startswith(SKIP) and b"://" not in x]
        pages.append((title, redirect, links, lens))
        pos = data.find(b"  <page>\n", end)
    return pages


def reorder(pages, order_path):
    """cmix -e's article_reorder.h, reproduced: order lines index non-redirect pages; unused pages appended."""
    remap = {}
    c1 = c2 = -1
    redirect = False
    for i, (_, r, _, _) in enumerate(pages):
        remap[c2] = c1
        if not redirect:
            c2 += 1
        c1 += 1
        redirect = r
    # (the C++ maps the last count2 only at the next '<page>', i.e. never for the last page; same here)
    pos, used = [], [0] * len(pages)
    for line in open(order_path):
        res = remap.get(int(line), 0)
        pos.append(res)
        used[res] = 1
    pos += [i for i in range(len(pages)) if not used[i]]
    return pos


def stats(pages, order, label, ks=(1, 10, 100, 1000)):
    seen = collections.Counter()
    titles = set()
    recent = collections.deque(maxlen=max(ks))
    agg = collections.defaultdict(lambda: [0, 0])
    nl = nb = 0
    for idx in order:
        title, red, links, lens = pages[idx]
        in_art = set()
        rec = list(recent)
        recpos = {t: i for i, t in enumerate(reversed(rec))}  # 0 = previous page
        for t, ln in zip(links, lens):
            c = seen[t]
            if t in in_art:
                b = "seen, same article"
            elif c:
                b = "seen in another article, count " + ("1" if c == 1 else "2-4" if c < 5 else "5-19" if c < 20 else "20-99" if c < 100 else "100+")
            elif t in titles:
                b = "seen only as an earlier title"
            else:
                b = "never seen"
            agg[b][0] += 1
            agg[b][1] += ln
            if b in ("seen only as an earlier title", "never seen") and t in recpos:
                for k in ks:
                    if recpos[t] < k:
                        agg[f"  (of those: title of one of the previous {k} pages)"][0] += 1
                        agg[f"  (of those: title of one of the previous {k} pages)"][1] += ln
            nl += 1
            nb += ln
            in_art.add(t)
        for t in in_art:
            pass
        for t in links:
            seen[t] += 1
        titles.add(title)
        recent.append(title)
    print(f"\n### {label}: {len(order):,} pages, {nl:,} links, {nb:,} target bytes (text, incl. terminator)\n")
    print("| bucket | links | share of links | target bytes | share of bytes |\n|---|---:|---:|---:|---:|")
    for b in sorted(agg):
        l, by = agg[b]
        print(f"| {b} | {l:,} | {100 * l / nl:.1f}% | {by:,} | {100 * by / nb:.1f}% |")
    sys.stdout.flush()


def main():
    pages = parse(sys.argv[1])
    nred = sum(r for _, r, _, _ in pages)
    print(f"{len(pages):,} pages, {nred:,} redirects")
    if "--slice-pages" in sys.argv:
        n = int(sys.argv[sys.argv.index("--slice-pages") + 1])
        stats(pages, range(n), f"identity order, first {n} pages (slice calibration)")
    stats(pages, range(len(pages)), "identity order, full enwik9")
    order = reorder(pages, sys.argv[2])
    assert sorted(order) == list(range(len(pages))), "reorder is not a permutation"
    stats(pages, order, "record's reordered order (cmix -e), full enwik9")


if __name__ == "__main__":
    main()
