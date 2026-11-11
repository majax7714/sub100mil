#!/usr/bin/env python3
"""X3c: string-overlap seriation (genome-assembly lens). Re-order a page set by greedy nearest-neighbour chaining
on MinHash similarity of word 4-gram shingles, i.e. by shared TEXT (what PPMD's order-25 contexts and the match
models can use) instead of by embedding similarity (what the record's order uses).

  python3 -P -E scripts/analysis/x3c_seriate.py SAMPLE.xml.pages OUT_ORDER.txt [--k 96] [--mix 0.0] [--local 0]
SAMPLE.xml.pages: page indices in a baseline order (x3c_mksample.py). OUT: the new order (page indices).
Greedy chain: start at the baseline's first page; step to the unvisited page with the highest estimated Jaccard to
the current page (ties and zero similarity: the next unvisited page in baseline order). --local N restricts the
candidates to the next N unvisited pages of the baseline order (keeps the baseline's coarse structure).
"""
import argparse
import os
import re
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x3c_mksample as ms  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pages"); ap.add_argument("out")
    ap.add_argument("--k", type=int, default=96)
    ap.add_argument("--local", type=int, default=0)
    ap.add_argument("--minsim", type=float, default=0.06)
    a = ap.parse_args()
    mm, st, en, rop, par = ms.load()
    pages = [int(x) for x in open(a.pages).read().split()]
    n = len(pages)
    rng = np.random.default_rng(923)
    A = rng.integers(1, 2**63, size=a.k, dtype=np.uint64) | np.uint64(1)
    B = rng.integers(0, 2**63, size=a.k, dtype=np.uint64)
    sig = np.zeros((n, a.k), dtype=np.uint64)
    tok = re.compile(rb"[A-Za-z0-9\x80-\xff]+|[^\sA-Za-z0-9\x80-\xff]")
    for i, p in enumerate(pages):
        t = mm[int(st[p]):int(en[p])]
        j = t.find(b"<text")
        w = tok.findall(t[j:] if j >= 0 else t)
        if len(w) < 4:
            h = np.array([zlib.crc32(t)], dtype=np.uint64)
        else:
            hs = np.array([zlib.crc32(x) for x in w], dtype=np.uint64)
            h = hs[:-3] * np.uint64(0x9E3779B1) ^ (hs[1:-2] << np.uint64(13)) ^ (hs[2:-1] << np.uint64(27)) ^ (hs[3:] << np.uint64(41))
            h = np.unique(h)
            if len(h) > 4000:
                h = h[:4000]  # unique() sorts: an unbiased sample of the hashed shingles
        with np.errstate(over="ignore"):
            sig[i] = (h[:, None] * A[None, :] + B[None, :]).min(axis=0)
    sig32 = (sig >> np.uint64(32)).astype(np.uint32)
    S = np.zeros((n, n), dtype=np.uint8)
    for lo in range(0, n, 256):
        hi = min(n, lo + 256)
        S[lo:hi] = (sig32[lo:hi, None, :] == sig32[None, :, :]).sum(axis=2).astype(np.uint8)
    thr = int(a.minsim * a.k)
    visited = np.zeros(n, dtype=bool)
    order = []; cur = 0; nxt_base = 0; jumps = 0
    for _ in range(n):
        order.append(cur); visited[cur] = True
        row = S[cur].copy(); row[visited] = 0
        if a.local:
            cand = np.flatnonzero(~visited)[:a.local]
            mask = np.zeros(n, dtype=bool); mask[cand] = True; row[~mask] = 0
        b = int(row.argmax())
        if row[b] >= thr and row[b] > 0:
            cur = b
        else:
            while nxt_base < n and visited[nxt_base]:
                nxt_base += 1
            if nxt_base >= n:
                break
            cur = nxt_base; jumps += 1
    assert len(order) == n and len(set(order)) == n
    open(a.out, "w").write("\n".join(str(pages[i]) for i in order) + "\n")
    adj = np.array([S[order[i], order[i + 1]] for i in range(n - 1)]) / a.k
    base = np.array([S[i, i + 1] for i in range(n - 1)]) / a.k
    print(f"{n} pages; mean adjacent Jaccard: baseline {base.mean():.4f}, seriated {adj.mean():.4f}; chain restarts {jumps}")


if __name__ == "__main__":
    main()
