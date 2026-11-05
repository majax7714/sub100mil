#!/usr/bin/env python3
"""E11: text-level hash of every coded-stream token of a slice, for global-document-frequency labels.

Usage:
  e11_tokhash.py --cost COSTLOG --pre SLICE.PRE --dict english.dic --out TOKHASH.bin

Tokens are e11_sim.cpp's: maximal runs of word bytes in the CODED stream (logical a-z, A-Z, 0-9, >= 0x80, case flags
0x40/0x07/0x06, escape 0x0C plus its byte), in stream order. For each token, the decoded text from its first byte up
to its terminator is taken (A1's verified WRT decoder), and its leading run of text word characters ([A-Za-z0-9] or
>= 0x80, e11_fullscale.cpp's tokenizer) is hashed with e11_fullscale.cpp's FNV-1a variant. Writes one uint64 per
token (0 if the decoded text starts with no word character).
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a1_analyze as a1  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
import costlog  # noqa: E402

M64 = (1 << 64) - 1


def wordc(c):
    return 97 <= c <= 122 or 65 <= c <= 90 or 48 <= c <= 57 or c >= 0x80


def fnv(b):
    h = 1469598103934665603
    for c in b:
        h = ((h ^ c) * 1099511628211) & M64
    return h ^ (h >> 31)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cost", required=True)
    ap.add_argument("--pre", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    r = costlog.load(args.cost, mmap=False)
    stream = r["byte"].tobytes()
    text, cmap, _ = a1.wrt_decode(stream, a1.load_dict(args.dict))
    if text != open(args.pre, "rb").read():
        raise SystemExit("WRT decode mismatch")
    sw = a1.SWAP
    out = []
    esc = False
    ts = -1
    nempty = 0
    for i, b in enumerate(stream):
        c = sw[b]
        if esc:
            w, esc = True, False
        else:
            w = wordc(c) or c in (0x40, 0x07, 0x06, 0x0C)
            esc = c == 0x0C
        if w:
            if ts < 0:
                ts = i
            continue
        if ts >= 0:
            t = text[cmap[ts]:cmap[i]]
            k = 0
            while k < len(t) and wordc(t[k]):
                k += 1
            if k == 0:
                nempty += 1
            out.append(fnv(t[:k]) if k else 0)
            ts = -1
    np.array(out, np.uint64).tofile(args.out)
    print(f"{len(out):,} tokens ({nempty:,} without a text word) -> {args.out}")


if __name__ == "__main__":
    main()
