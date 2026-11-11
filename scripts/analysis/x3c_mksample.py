#!/usr/bin/env python3
"""X3c: write an enwik9 sample (intro + whole pages + closing coda) with a chosen page set and page order, for
slice-mode runs of the real stack (slicetool pre/post round-trips it: the intro is enwik9's first 29 lines).

  x3c_mksample.py --set rank:A:B | page:A:B | bytes:rank:A:NBYTES | bytes:page:A:NBYTES   (rank = position in the
                  record's full-run order, page = original page index)  --order rec|id|rand[:seed]|file:PATH  --out F
  file:PATH = text file of page indices (a full order; restricted to the set).
Also importable: load() returns (mm, starts, ends, rank_of_page, page_at_rank).
"""
import argparse
import mmap
import os
import random
import re

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IDX = os.path.join(REPO, "build/exp/X3c/page_index.npz")


def load():
    f = open(os.path.join(REPO, "data/raw/enwik9"), "rb")
    mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    if not os.path.exists(IDX):
        starts = np.array([m.start() + 1 for m in re.finditer(rb"\n  <page>\n", mm)], dtype=np.int64)
        ends = np.array([m.end() for m in re.finditer(rb"\n  </page>\n", mm)], dtype=np.int64)
        assert len(starts) == len(ends) + 1  # the last page is cut by the end of enwik9 (it is the coda)
        starts = starts[:len(ends)]
        par = []
        for line in open(os.path.join(REPO, "build/exp/X2/meta_pages.tsv")).readlines()[1:]:
            par.append(int(line.split("\t")[1]))
        par = np.array([x for x in par if x < len(starts)], dtype=np.int64)  # the tsv also lists the coda's cut page
        assert len(par) == len(starts) and len(set(par.tolist())) == len(par)
        np.savez(IDX, starts=starts, ends=ends, page_at_rank=par)
    z = np.load(IDX)
    par = z["page_at_rank"]
    rop = np.empty(len(par), dtype=np.int64); rop[par] = np.arange(len(par))
    return mm, z["starts"], z["ends"], rop, par


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", required=True)
    ap.add_argument("--order", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--stub", action="store_true", help="metadata proxy (as x2_proxy.py --strip-comments): replace each "
                    "page's text by a one-line stub and drop <comment>/<minor /> lines, so ~2,800 records fit in 1 MB")
    ap.add_argument("--window", type=int, default=0, help="after ordering, sort page indices inside consecutive "
                    "windows of this many pages (the windowed id-sort family)")
    a = ap.parse_args()
    mm, st, en, rop, par = load()
    s = a.set.split(":")
    if s[0] == "bytes":
        seq = par if s[1] == "rank" else np.arange(len(st))
        i = int(s[2]); tot = 0; pages = []
        while i < len(seq) and tot < int(s[3]):
            p = int(seq[i]); pages.append(p); tot += int(en[p] - st[p]); i += 1
    elif s[0] == "rank":
        pages = [int(p) for p in par[int(s[1]):int(s[2])]]
    else:
        pages = list(range(int(s[1]), int(s[2])))
    o = a.order
    if o == "id":
        pages.sort()
    elif o == "rec":
        pages.sort(key=lambda p: rop[p])
    elif o.startswith("rand"):
        pages.sort(); random.Random(int(o.split(":")[1]) if ":" in o else 1).shuffle(pages)
    elif o.startswith("file:"):
        pos = {int(x): i for i, x in enumerate(open(o[5:]).read().split())}
        pages.sort(key=lambda p: pos[p])
    if a.window:
        pages = [p for i in range(0, len(pages), a.window) for p in sorted(pages[i:i + a.window])]
    intro_end = int(st[0])
    with open(a.out, "wb") as f:
        f.write(mm[:intro_end])
        for p in pages:
            b = mm[int(st[p]):int(en[p])]
            if a.stub:
                out = []; title = b""; intext = False
                for line in b.splitlines(keepends=True):
                    t = line.strip()
                    if t.startswith(b"<title>"):
                        title = t[7:-8]
                    if intext:
                        intext = b"</text>" not in line
                        continue
                    if t.startswith(b"<comment>") or t == b"<minor />":
                        continue
                    if t.startswith(b"<text"):
                        out.append(b'      <text xml:space="preserve">#REDIRECT [[' + title + b']]</text>\n')
                        intext = b"</text>" not in line and not t.endswith(b"/>")
                        continue
                    out.append(line)
                b = b"".join(out)
            f.write(b)
        f.write(b"</mediawiki>\n")
    with open(a.out + ".pages", "w") as f:
        f.write("\n".join(map(str, pages)) + "\n")
    print(f"{a.out}: {len(pages)} pages, {os.path.getsize(a.out):,} B")


if __name__ == "__main__":
    main()
