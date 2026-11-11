#!/usr/bin/env python3
"""X3c: the "windowed id-sort" family of article orders. Take the record's order file and sort the article numbers
inside consecutive windows of W ranks (W = 1: the record's order; W >= 172,277: identity order among articles).
Writes build/exp/X3c/orders/order_wW (article numbers, the format of new_article_order), order_wW.rd (P1 rank-delta
text) and pid_wW.txt (the metadata block's page-id delta lines for that order: articles, then redirects in id order),
and prints quick sizes (xz -9e, 7z PPMd) of both streams. The real sizes come from cmix (x3c_ordersize.sh) and from
real-stack metadata proxies.

  python3 -P -E scripts/analysis/x3c_orders.py 1 4 16 64 256 1024 4096 16384 200000
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "build/exp/X3c/orders")
ORDER = os.path.join(REPO, "third_party/fx2-cmix-transformer-v1/src/readalike_prepr/data/new_article_order")


def rankdelta(ids):
    M = max(ids) + 1
    fw = [0] * (M + 1)
    for i in range(1, M + 1):
        fw[i] = i & -i

    def pre(i):
        s = 0
        while i > 0:
            s += fw[i]; i -= i & -i
        return s

    def rem(i):
        i += 1
        while i <= M:
            fw[i] -= 1; i += i & -i
    out, prev = [M], None
    for x in ids:
        out.append(pre(x) - (pre(prev) if prev is not None else 0)); rem(x); prev = x
    return out


def sizes(path):
    xz = len(subprocess.run(["xz", "-9e", "-c", path], capture_output=True).stdout)
    tmp = path + ".7z"
    if os.path.exists(tmp):
        os.remove(tmp)
    subprocess.run(["7z", "a", "-m0=PPMd:o=6:mem=256m", tmp, path], capture_output=True)
    pp = os.path.getsize(tmp); os.remove(tmp)
    return xz, pp


def main():
    os.makedirs(OUT, exist_ok=True)
    ids = [int(x) for x in open(ORDER).read().split()]
    # article number -> page id, redirect page ids (from X2's table: rank, page, pid, ..., art)
    art_pid = {}; red = []
    for line in open(os.path.join(REPO, "build/exp/X2/meta_pages.tsv")).readlines()[1:]:
        f = line.rstrip("\n").split("\t")
        if f[2] == "None":
            continue
        if int(f[8]) >= 0:
            art_pid[int(f[8])] = int(f[2])
        else:
            red.append(int(f[2]))
    print("W\torder.rd xz\torder.rd ppmd\tpid xz\tpid ppmd")
    for w in map(int, sys.argv[1:]):
        o = []
        for i in range(0, len(ids), w):
            o += sorted(ids[i:i + w])
        p = os.path.join(OUT, f"order_w{w}")
        open(p, "w").write("".join(f"{x}\n" for x in o))
        open(p + ".rd", "w").write("".join(f"{x}\n" for x in rankdelta(o)))
        pids = [art_pid[a] for a in o] + red
        q = os.path.join(OUT, f"pid_w{w}.txt")
        prev = 0; L = []
        for x in pids:
            L.append(f">{x - prev}\n"); prev = x
        open(q, "w").write("".join(L))
        a = sizes(p + ".rd"); b = sizes(q)
        print(f"{w}\t{a[0]:,}\t{a[1]:,}\t{b[0]:,}\t{b[1]:,}", flush=True)


if __name__ == "__main__":
    main()
