#!/usr/bin/env python3
"""X2: causal slots for the domain-law candidates, in E12's slot format (input of x2_sim.cpp / e12_sim.cpp).

Usage:
  python3 -P -E scripts/analysis/x2_slots.py --pre SLICE.PRE --cost COSTLOG --dict english.dic --out SLOTS.bin
         [--classes isbn,ordinal,chrono,yearart] [--alpha 1.0]

Every slot opens on a LEFT-CONTEXT trigger only (no look-ahead), so false triggers are included and paid for:
  isbn     after "ISBN" + 9 digits (single '-' or ' ' allowed between them): candidates check char, "-"+check, " "+check
  ordinal  after every digit of a free-standing digit run (<= 4 digits): candidate = the run's English ordinal suffix
           ("st", "nd", "rd", "th"); when a digit follows instead, the specialist abstains for that slot
  chrono   at the start of a list line ("* ", "*[", "*'''", "# ") when the previous list line (<= 3 lines back, no
           heading between) began with a 4-digit year y0: candidates 4-digit years y >= y0 weighted (1 + y - y0)^-alpha
           (a weight of `back` for each year below y0 down to y0 - 50, for unsorted lists)
  yearart  year articles (title = year): the Roman numeral after "[Roman numerals|", the weekday after "starting on "
The candidate strings are in the coded (WRT) form: a law's value is used in the coded form of the actual text when it
equals the text (the coded form is a function of the text), else in plain text (it then mismatches and abstains).
Format: see e12_slots.py (descriptor kinds 0 = LIST only).
"""
import argparse
import os
import re
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a1_analyze as a1  # noqa: E402
import x2_census as xc  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
import costlog  # noqa: E402

CLASSES = ["isbn", "ordinal", "chrono", "yroman", "ywday"]
CI = {c: i for i, c in enumerate(CLASSES)}

RE_ISBN9 = re.compile(rb"ISBN(?:-10)?:?\s*((?:\d[ -]?){8}\d)")
RE_DIGRUN = re.compile(rb"(?<![\d.,])\d{1,4}")
RE_LIST = re.compile(rb"^(?:\*+|#+) ?(?:'''|'')?\[?", re.M)
RE_LISTY = re.compile(rb"^(?:\*+|#+) ?(?:'''|'')?\[?(\d{4})(?![\d,.])", re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True)
    ap.add_argument("--cost", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--classes", default="isbn,ordinal,chrono,yearart")
    ap.add_argument("--alpha", type=float, default=1.0)
    ap.add_argument("--back", type=float, default=0.02)
    args = ap.parse_args()
    want = set(args.classes.split(","))

    text = open(args.pre, "rb").read()
    r = costlog.load(args.cost, mmap=False)
    stream = r["byte"].tobytes()
    dec, cmap, container = a1.wrt_decode(stream, a1.load_dict(args.dict))
    if text[:len(dec)] != dec:
        raise SystemExit("WRT decode mismatch")
    text = dec
    first = np.full(len(text) + 1, -1, np.int64)
    idx = np.arange(len(cmap))
    ok = ~container
    first[cmap[ok][::-1]] = idx[ok][::-1]
    # a text position starts a coded byte only if first[pos] maps back exactly (not inside a dictionary word)
    def cpos(p):
        c = first[p]
        return c if c >= 0 and cmap[c] == p else -1

    def coded(s, e):
        a, b = cpos(s), cpos(e) if e < len(text) else len(stream)
        if a < 0 or b < 0:
            return None
        return stream[a:b]

    slots = []  # (coded start, class, actual coded bytes, [(cand, w)])

    def add(pos, cls, cands, actual_len_hint=None):
        c = cpos(pos)
        if c < 0:
            return
        cands = [(s, w) for s, w in cands if s]
        if not cands:
            return
        # actual = the coded bytes that match the longest candidate prefix, else one coded byte
        actual = stream[c:c + 1]
        for s, w in sorted(cands, key=lambda t: -len(t[0])):
            if stream[c:c + len(s)] == s:
                actual = s
                break
        slots.append((c, CI[cls], actual, cands))

    starts = xc.pages_of(text)
    ordsuf_coded = {}
    stats = {k: [0, 0] for k in CLASSES}
    for pi in range(len(starts) - 1):
        a, b = starts[pi], starts[pi + 1]
        p = text[a:b]
        tm = re.search(rb"<title>(.*?)</title>", p)
        title = tm.group(1) if tm else b""
        if "isbn" in want:
            for m in RE_ISBN9.finditer(p):
                d = bytes(ch for ch in m.group(1) if ch in b"0123456789")
                if len(d) != 9:
                    continue
                s = sum((10 - i) * (ch - 48) for i, ch in enumerate(d))
                chk = (11 - s % 11) % 11
                ch = b"X" if chk == 10 else bytes([48 + chk])
                pos = a + m.end(1)
                # coded form of the check char: take it from the text when the text has it right after
                cands = []
                for pre, w in ((b"", 0.7), (b"-", 0.25), (b" ", 0.05)):
                    t0 = pos + len(pre)
                    if text[pos:t0] == pre and text[t0:t0 + 1].upper() == ch:
                        cs = coded(pos, t0 + 1)
                    else:
                        cs = pre + ch
                    cands.append((cs, w))
                stats["isbn"][0] += 1
                add(pos, "isbn", cands)
        if "ordinal" in want:
            for m in RE_DIGRUN.finditer(p):
                run = m.group(0)
                for k in range(1, len(run) + 1):
                    n = int(run[:k])
                    suf = xc.ordsuf(n)
                    pos = a + m.start() + k
                    if k == len(run) and text[pos:pos + 2] == suf and not text[pos + 2:pos + 3].isalpha():
                        cs = coded(pos, pos + 2)
                        if cs is not None:
                            ordsuf_coded.setdefault(suf, cs)
                            stats["ordinal"][1] += 1
                    cs = ordsuf_coded.get(suf, suf)
                    stats["ordinal"][0] += 1
                    add(pos, "ordinal", [(cs, 1.0)])
        if "chrono" in want:
            lines = list(RE_LIST.finditer(p))
            yl = {m.start(): int(m.group(1)) for m in RE_LISTY.finditer(p)}
            prev_y, prev_end = None, -1
            for m in lines:
                if prev_y is not None:
                    between = p[prev_end:m.start()]
                    if b"\n=" not in between and between.count(b"\n") <= 3 and prev_y >= 1000:
                        cands = []
                        for y in range(max(1000, prev_y - 50), min(9999, prev_y + 300) + 1):
                            w = (1.0 + y - prev_y) ** -args.alpha if y >= prev_y else args.back / 50
                            cands.append((str(y).encode(), w))
                        stats["chrono"][0] += 1
                        stats["chrono"][1] += m.start() in yl
                        add(a + m.end(), "chrono", cands)
                y = yl.get(m.start())
                prev_y = y
                prev_end = m.end()
        if "yearart" in want and title.isdigit() and 1 <= int(title) <= 2100:
            y = int(title)
            m = re.search(rb"\[Roman numerals\|", p[:3000])
            if m:
                pos = a + m.end()
                rn = xc.roman(y)
                cs = coded(pos, pos + len(rn)) if text[pos:pos + len(rn)] == rn else rn
                stats["yroman"][0] += 1
                add(pos, "yroman", [(cs, 1.0)])
            m = re.search(rb"year starting on \[?", p[:4000])
            if m:
                pos = a + m.end()
                cands = []
                for greg in (True, False):
                    wd = xc.WDAYS[xc.weekday(y, 1, 1, greg)]
                    cs = coded(pos, pos + len(wd)) if text[pos:pos + len(wd)] == wd else wd
                    cands.append((cs, 0.8 if greg == (y >= 1583) else 0.2))
                stats["ywday"][0] += 1
                add(pos, "ywday", cands)
    # drop overlapping slots (keep the earlier)
    slots.sort(key=lambda s: s[0])
    out, last_end = [], -1
    for s in slots:
        if s[0] >= last_end:
            out.append(s)
            last_end = s[0] + len(s[2])
    print(f"{len(out):,} slots ({len(slots) - len(out):,} overlapping dropped); triggers/true per class: {stats}")
    with open(args.out, "wb") as f:
        f.write(struct.pack("<q", len(out)))
        for c, cls, actual, cands in out:
            f.write(struct.pack("<qBBB", int(c), cls, 1, len(actual)) + actual)
            f.write(struct.pack("<i", 0))
            agg = {}
            for s, w in cands:
                agg[s] = agg.get(s, 0.0) + w
            items = list(agg.items())[:65535]
            f.write(struct.pack("<Bf", 0, 1.0) + struct.pack("<H", len(items)))
            for s, w in items:
                f.write(struct.pack("<B", len(s)) + s + struct.pack("<f", w))


if __name__ == "__main__":
    main()
