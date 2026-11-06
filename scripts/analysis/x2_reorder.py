#!/usr/bin/env python3
"""X2: per-page metadata of enwik9 in the record's full-run (-e, reordered) page order.

Usage: python3 -P -E scripts/analysis/x2_reorder.py [--enwik9 data/raw/enwik9]
           [--order third_party/fx2-cmix-transformer-v1/src/readalike_prepr/data/new_article_order]
           --out build/exp/X2/meta_pages.tsv

The full run reorders pages (article_reorder.h, mirrored exactly below: non-redirect article numbers from the
order file are remapped to page indices, then the unused pages follow in id order) BEFORE phda9, which moves every
page's revision id, timestamp and contributor into one block after the last page, in that order.
Writes one row per page in reordered order: rank, original page index, page id, revision id, unix time, user/ip,
user id (empty for ip), redirect flag, article number from the order file (-1 for the appended pages; the
decoder knows it for every record, since the order file is decoded first).
"""
import argparse
import calendar
import re

PREFIX = [b'      <text xml:space="preserve">#REDIRECT', b'      <text xml:space="preserve">#redirect',
          b'      <text xml:space="preserve">#Redirect', b'      <text xml:space="preserve">#REdirect',
          b'      <text xml:space="preserve">{{softredirect']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--enwik9", default="data/raw/enwik9")
    ap.add_argument("--order", default="third_party/fx2-cmix-transformer-v1/src/readalike_prepr/data/new_article_order")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    pages = []
    remap = {}
    count1 = count2 = -1
    redirect = False
    cur = None
    state = None
    with open(a.enwik9, "rb") as f:
        for line in f:
            line = line.rstrip(b"\n")
            for pre in PREFIX:
                if line.startswith(pre):
                    redirect = True
                    break
            if line == b"  <page>":
                remap[count2] = count1
                if not redirect:
                    count2 += 1
                count1 += 1
                redirect = False
                cur = {"pid": None, "rev": None, "t": None, "user": None, "uid": "", "redir": 0}
                pages.append(cur)
                state = "page"
                continue
            if cur is None:
                continue
            s = line.strip()
            if s.startswith(b"<text") and any(line.startswith(p) for p in PREFIX):
                cur["redir"] = 1
            if s == b"<revision>":
                state = "rev"
            elif s == b"<contributor>":
                state = "contrib"
            elif s.startswith(b"<id>"):
                v = int(s[4:-5])
                if state == "page" and cur["pid"] is None:
                    cur["pid"] = v
                elif state == "rev" and cur["rev"] is None:
                    cur["rev"] = v
                elif state == "contrib":
                    cur["uid"] = str(v)
            elif s.startswith(b"<timestamp>"):
                ts = s[11:-12].decode()
                cur["t"] = calendar.timegm((int(ts[0:4]), int(ts[5:7]), int(ts[8:10]), int(ts[11:13]), int(ts[14:16]),
                                            int(ts[17:19])))
            elif s.startswith(b"<username>"):
                cur["user"] = s[10:-11].decode("utf8", "replace")
            elif s.startswith(b"<ip>"):
                cur["user"] = "ip:" + s[4:-5].decode("utf8", "replace")
    n = len(pages)
    positions = []
    arts = []
    used = [0] * n
    with open(a.order) as f:
        for line in f:
            res = remap.get(int(line), 0)
            positions.append(res)
            arts.append(int(line))
            used[res] = 1
    if len(positions) < n:
        rest = [i for i in range(n) if not used[i]]
        positions += rest
        arts += [-1] * len(rest)
    with open(a.out, "w") as f:
        f.write("rank\tpage\tpid\trev\tt\tuser\tuid\tredir\tart\n")
        for rank, i in enumerate(positions):
            p = pages[i]
            f.write(f"{rank}\t{i}\t{p['pid']}\t{p['rev']}\t{p['t']}\t{(p['user'] or '').replace(chr(9), ' ')}\t{p['uid']}\t"
                    f"{p['redir']}\t{arts[rank]}\n")
    print(f"{n:,} pages, {len(positions):,} positions, {len(set(positions)):,} distinct")


if __name__ == "__main__":
    main()
