#!/usr/bin/env python3
"""E11 extraction: per coded byte, the article it belongs to and its A1 content class.

Usage:
  e11_extract.py --cost COSTLOG --pre SLICE.PRE --dict english.dic --out AUX.bin

Writes AUX.bin: n x int32 article id, then n x uint8 A1 class (scripts/analysis/a1_analyze.py's CLASSES order),
where n = number of coded bytes in COSTLOG. Article id: the index of the `  <page>` the decoded character lies in
(from the page's first byte up to the next page's first byte); -1 for the slice header; -2 - k for section k after
the articles (metadata block, interlanguage block, XML tail). The coded stream is WRT-decoded with A1's verified
decoder (checked byte-for-byte against SLICE.PRE).
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
    bcls = tcls[cmap].astype(np.uint8)
    bcls[container] = a1.C["container (block hdr)"]
    art_end = sections["articles"][1]
    pages = np.array([m.start() for m in re.finditer(rb"  <page>\n", text[:art_end])], np.int64)
    tid = np.searchsorted(pages, np.arange(len(text)), "right").astype(np.int64) - 1  # -1 before the first page
    for k, name in enumerate(("metadata", "interlanguage", "xml_tail")):
        a, b = sections[name]
        tid[a:b] = -2 - k
    aid = tid[cmap].astype(np.int32)
    with open(args.out, "wb") as f:
        f.write(aid.tobytes())
        f.write(bcls.tobytes())
    print(f"{len(stream):,} coded bytes, {len(pages):,} pages; wrote {args.out}")


if __name__ == "__main__":
    main()
