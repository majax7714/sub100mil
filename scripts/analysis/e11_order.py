#!/usr/bin/env python3
"""E11: dump the record's -e article order (page indices, one per line) for e11_fullscale.

Usage: e11_order.py FULL.PRE NEW_ARTICLE_ORDER OUT.txt
Reuses scripts/analysis/a2_fullscale.py's parse() and reorder() (cmix -e's article_reorder.h, reproduced).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a2_fullscale as a2  # noqa: E402

pages = a2.parse(sys.argv[1])
order = a2.reorder(pages, sys.argv[2])
assert sorted(order) == list(range(len(pages)))
with open(sys.argv[3], "w") as f:
    f.write("\n".join(map(str, order)) + "\n")
print(f"{len(order):,} pages written")
