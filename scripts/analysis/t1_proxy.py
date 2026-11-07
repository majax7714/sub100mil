#!/usr/bin/env python3
"""T1: an interlanguage-heavy proxy of the full run's interwiki block.

Writes enwik9's intro, then, in the full run's article order (x2_reorder.py's meta_pages.tsv), the pages of reordered
ranks [A, B) that own an interlanguage group, each with its text replaced by a stub ("'''Title'''") plus the
original trailing interlanguage lines, then the closing tag. After `slicetool pre` its tail is the full run's
interwiki block for those ranks: the same groups in the same order, so the record's local context (transformer
window, recent CM history) on the block is the full run's, while the article text is gone.

Usage: python3 -P -E scripts/analysis/t1_proxy.py --ranks 100000:110000 --meta build/exp/X2/meta_pages.tsv \
           --out build/exp/T1/iwproxy_100k.xml [--max-bytes 1000000]
"""
import argparse
import pickle
import re

REF = {}
CTX = 1

RE_IWL = re.compile(rb"^\[\[[a-z][a-z\-]{1,11}:[^\]\n]*\]\]\s*$")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--enwik9", default="data/raw/enwik9")
    ap.add_argument("--meta", required=True)
    ap.add_argument("--ranks", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-bytes", type=int, default=1000000)
    ap.add_argument("--ctx", type=int, default=1, help="non-blank original lines kept above the tail")
    ap.add_argument("--ref", default="build/exp/T1/full_groups.pkl", help="title -> full-run group (check_proxy.py)")
    a = ap.parse_args()
    global REF, CTX
    CTX = a.ctx
    REF = pickle.load(open(a.ref, "rb"))
    A, B = (int(x) for x in a.ranks.split(":"))
    want = []
    with open(a.meta) as f:
        next(f)
        for line in f:
            rank, page = line.split("\t")[:2]
            if A <= int(rank) < B:
                want.append(int(page))
    wset = set(want)
    pages = {}
    intro = []
    cur, idx, intext, body = None, -1, False, []
    title = b""
    with open(a.enwik9, "rb") as f:
        for n, line in enumerate(f):
            if n < 29:
                intro.append(line)
                continue
            if line == b"  <page>\n":
                idx += 1
                cur = [line] if idx in wset else None
                continue
            if cur is None:
                continue
            s = line.strip()
            if s.startswith(b"<title>"):
                title = s[7:-8]
            if intext:
                body.append(line)
                if b"</text>" in line:
                    intext = False
                    finish(cur, body, title)
                    if cur[-1] is not None:
                        pages[idx] = cur
                continue
            if s.startswith(b"<comment>") or s == b"<minor />":
                continue
            if s.startswith(b"<text"):
                if b"</text>" in line or s.endswith(b"/>"):
                    cur = None  # one-line text: no trailing interlanguage lines
                    continue
                intext = True
                body = [line]
                cur.append(b"@TEXT@")
                continue
            cur.append(line)
    out, nb, npg = [], sum(len(x) for x in intro) + 14, 0
    for p in want:
        if p not in pages:
            continue
        pg = [x for x in pages[p] if x is not None]
        sz = sum(len(x) for x in pg)
        if nb + sz > a.max_bytes:
            break
        out.append(pg); nb += sz; npg += 1
    with open(a.out, "wb") as f:
        f.writelines(intro)
        for pg in out:
            f.writelines(pg)
        f.write(b"</mediawiki>\n")
    print(f"{npg:,} pages with interlanguage tails written, {nb:,} B")


def finish(cur, body, title):
    """Replace the @TEXT@ marker with a stub + the original text from just above the interlanguage tail that
    phda9 moves in the full run (its first link is taken from the full run's group, REF), or drop the page."""
    i = cur.index(b"@TEXT@")
    g = REF.get(title)
    m = re.match(rb"\[([a-z][a-z\-]{1,11}):", g or b"")
    lines = b"".join(body).split(b"\n")
    j = -1
    if m:
        key = b"[[" + m.group(1) + b":"
        for k in range(len(lines) - 1, -1, -1):
            if lines[k].lstrip().startswith(key) or (k == 0 and key in lines[k]):
                j = k
                break
    if j < 1:
        cur[i] = None
        cur.append(None)
        return
    # phda9 needs the context above the tail to move all of it (with a bare stub it moves only part): keep the
    # last non-blank line above it (usually the last category) and the blank lines in between
    k = j
    for _ in range(CTX):
        k -= 1
        while k > 0 and lines[k].strip() == b"":
            k -= 1
    k = max(k, 1)
    stub = b'      <text xml:space="preserve">' + b"'''" + title + b"'''\n\n"
    cur[i] = stub + b"\n".join(lines[k:])


if __name__ == "__main__":
    main()
