#!/usr/bin/env python3
"""X2: build a metadata-heavy proxy of the full run's metadata block: enwik9's intro, then the pages of reordered
ranks [A, B) (the record's -e article order, see x2_reorder.py) with each page's text replaced by a one-line stub,
then the closing tag. After `slicetool pre`, its tail is a (B - A)-record metadata block in the full run's order,
so a cost-logged record run on it measures what the record pays on reordered metadata (observation only).

Usage: python3 -P -E scripts/analysis/x2_proxy.py --ranks 100000:130000 --meta build/exp/X2/meta_pages.tsv \
           --out build/exp/X2/proxy_100k.xml
"""
import argparse


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--enwik9", default="data/raw/enwik9")
    ap.add_argument("--meta", required=True)
    ap.add_argument("--ranks", required=True)
    ap.add_argument("--by-page", action="store_true", help="select ORIGINAL page indices [A, B) and keep them in id "
                    "order (an identity-order control: the metadata block as it would be without the reorder)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--strip-comments", action="store_true", help="drop <comment> and <minor /> lines (page part only;"
                    " the metadata block does not contain them) to fit more records per byte")
    a = ap.parse_args()
    A, B = (int(x) for x in a.ranks.split(":"))
    want = []
    with open(a.meta) as f:
        next(f)
        for line in f:
            rank, page = line.split("\t")[:2]
            if A <= int(page if a.by_page else rank) < B:
                want.append(int(page))
    if a.by_page:
        want.sort()
    wset = set(want)
    pages = {}
    intro = []
    cur, idx, intext = None, -1, False
    with open(a.enwik9, "rb") as f:
        for n, line in enumerate(f):
            if n < 29:
                intro.append(line)
                continue
            if line == b"  <page>\n":
                idx += 1
                cur = [line] if idx in wset else None
                if cur is not None:
                    pages[idx] = cur
                title = b""
                continue
            if cur is None:
                continue
            s = line.strip()
            if s.startswith(b"<title>"):
                title = s[7:-8]
            if intext:
                if b"</text>" in line:
                    intext = False
                continue
            if a.strip_comments and (s.startswith(b"<comment>") or s == b"<minor />"):
                continue
            if s.startswith(b"<text"):
                cur.append(b'      <text xml:space="preserve">#REDIRECT [[' + title + b']]</text>\n')
                if b"</text>" not in line and not s.endswith(b"/>"):
                    intext = True
                continue
            cur.append(line)
    with open(a.out, "wb") as f:
        f.writelines(intro)
        for p in want:
            f.writelines(pages[p])
        f.write(b"</mediawiki>\n")
    print(f"{len(want):,} pages written")


if __name__ == "__main__":
    main()
