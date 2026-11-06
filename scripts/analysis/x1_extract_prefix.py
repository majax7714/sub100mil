#!/usr/bin/env python3
"""X1: e11_extract.py for a PREFIX of a cost log (e.g. one still being written by a long run).

Usage:
  x1_extract_prefix.py --cost COSTLOG --pre SLICE.PRE --dict english.dic --out-cost PREFIX.bin --out AUX.bin

Keeps the coded bytes up to the last page start (coded pattern of "  <page>\\n    <title>") in the readable part of
COSTLOG, writes that prefix as a new cost log (same header) and its AUX file (int32 article id, uint8 A1 class per
coded byte, as e11_extract.py). The decoded prefix is checked byte-for-byte against the start of SLICE.PRE.
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

PAT = b"  L\xdf\x98N\n    L\xdf\x9bN"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cost", required=True)
    ap.add_argument("--pre", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--out-cost", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    sz = os.path.getsize(args.cost)
    n_all = (sz - 16) // costlog.DTYPE.itemsize
    r = np.fromfile(args.cost, dtype=costlog.DTYPE, count=n_all, offset=16)
    stream = r["byte"].tobytes()
    n = stream.rfind(PAT)
    if n <= 0:
        raise SystemExit("no page start in the readable prefix")
    r = r[:n]
    stream = stream[:n]
    text, cmap, container = a1.wrt_decode(stream, a1.load_dict(args.dict))
    with open(args.pre, "rb") as f:
        ref = f.read(len(text))
    if text != ref:
        k = next(i for i in range(len(text)) if text[i] != ref[i])
        raise SystemExit(f"WRT decode mismatch at text offset {k}")
    tcls, _ = a1.classify(text + b"  </page>\n")
    bcls = tcls[cmap].astype(np.uint8)
    bcls[container] = a1.C["container (block hdr)"]
    pages = np.array([m.start() for m in re.finditer(rb"  <page>\n", text)], np.int64)
    tid = np.searchsorted(pages, np.arange(len(text)), "right").astype(np.int64) - 1
    aid = tid[cmap].astype(np.int32)
    with open(args.cost, "rb") as f:
        head = f.read(16)
    with open(args.out_cost, "wb") as f:
        f.write(head)
        r.tofile(f)
    with open(args.out, "wb") as f:
        f.write(aid.tobytes())
        f.write(bcls.tobytes())
    print(f"{n:,} coded bytes ({len(text):,} text bytes, {len(pages):,} pages), record {r['cost'].sum() / 8:,.0f} B; "
          f"wrote {args.out_cost}, {args.out}")


if __name__ == "__main__":
    main()
