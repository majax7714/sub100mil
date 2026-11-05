#!/usr/bin/env python3
"""Summarize a1_far output (full-stream long-range repetition) as markdown tables.

Usage: a1_far_summary.py far_full.tsv [group_windows=5]
"""
import collections
import csv
import sys

DB = ["<1M", "1M-16M", "16M-60M", "60M-200M", ">200M"]


def main():
    rows = list(csv.DictReader(open(sys.argv[1]), delimiter="\t"))
    group = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    win_bytes = {}
    agg = collections.defaultdict(lambda: [0, 0])
    for r in rows:
        w = int(r["window"])
        win_bytes[w] = int(r["bytes"])
        a = agg[(w, int(r["k"]), r["dist"])]
        a[0] += int(r["valid"])
        a[1] += int(r["correct"])
    n = sum(win_bytes.values())
    print(f"Coded stream: {n:,} bytes (identity article order)\n")
    print("| k | match exists | most-recent occurrence right | accuracy | " + " | ".join(f"right, source {d}" for d in DB) + " |")
    print("|---:|---:|---:|---:|" + "---:|" * len(DB))
    for k in (16, 32, 64):
        v = sum(agg[(w, k, d)][0] for w in win_bytes for d in DB)
        c = sum(agg[(w, k, d)][1] for w in win_bytes for d in DB)
        cells = [f"{100 * sum(agg[(w, k, d)][1] for w in win_bytes) / n:.2f}%" for d in DB]
        print(f"| {k} | {100 * v / n:.2f}% | {100 * c / n:.2f}% | {c / v:.3f} | " + " | ".join(cells) + " |")
    print("\nBy position in the stream (k=32, share of the window's coded bytes):\n")
    print("| stream offset | right (any distance) | right, source >= 16M | right, source >= 60M |")
    print("|---|---:|---:|---:|")
    ws = sorted(win_bytes)
    for g in range(0, len(ws), group):
        part = ws[g:g + group]
        nb = sum(win_bytes[w] for w in part)
        c = sum(agg[(w, 32, d)][1] for w in part for d in DB)
        c16 = sum(agg[(w, 32, d)][1] for w in part for d in DB[2:])
        c60 = sum(agg[(w, 32, d)][1] for w in part for d in DB[3:])
        lo = sum(win_bytes[w] for w in ws[:g]) / 1e6
        print(f"| {lo:.0f}-{lo + nb / 1e6:.0f}M | {100 * c / nb:.1f}% | {100 * c16 / nb:.2f}% | {100 * c60 / nb:.2f}% |")


if __name__ == "__main__":
    main()
