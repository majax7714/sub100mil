#!/usr/bin/env python3
"""X2 target census: occurrences of "domain-law" derivable spans and the bits the record pays on them.

Usage:
  python3 -P -E scripts/analysis/x2_census.py --pre SLICE.PRE [--cost COSTLOG --dict english.dic] [--out OUT.tsv]
         [--sample N:MB]        # only N evenly spaced chunks of MB megabytes (page-aligned), scaled up

For every candidate a function finds instances in an article: (start, end, holds) text spans of the bytes a law
would fix (e.g. the check digit of an ISBN). `holds` says whether the law actually gives the right value there
(checksum valid, weekday correct, ...). With --cost the coded stream is WRT-decoded (A1's verified decoder) and
the record's bits on each span are summed (a coded byte's cost lands on the text position it produces).
Bits at stake = record bits on spans where the law holds; that bounds any specialist's gain on the candidate.

Articles with the Rambot census sentence are E12's territory: the numeric candidates skip them (ISBN etc. don't).
Python runs as `python3 -P -E` (numpy is in the user site-packages).
"""
import argparse
import datetime
import math
import os
import random
import re
import sys
from collections import defaultdict

import numpy as np

MONTHS = [b"January", b"February", b"March", b"April", b"May", b"June", b"July", b"August", b"September",
          b"October", b"November", b"December"]
MI = {m: i + 1 for i, m in enumerate(MONTHS)}
WDAYS = [b"Monday", b"Tuesday", b"Wednesday", b"Thursday", b"Friday", b"Saturday", b"Sunday"]
MON = rb"(" + b"|".join(MONTHS) + rb")"
WDR = rb"(" + b"|".join(WDAYS) + rb")"

# --------------------------------------------------------------------------- helpers


def jdn(y, m, d, greg=True):
    a = (14 - m) // 12
    yy = y + 4800 - a
    mm = m + 12 * a - 3
    j = d + (153 * mm + 2) // 5 + 365 * yy + yy // 4 - 32083
    if greg:
        j = d + (153 * mm + 2) // 5 + 365 * yy + yy // 4 - yy // 100 + yy // 400 - 32045
    return j


def weekday(y, m, d, greg=True):  # 0 = Monday
    return jdn(y, m, d, greg) % 7


def valid_date(y, m, d):
    try:
        datetime.date(y, m, d)
        return True
    except ValueError:
        return False


def roman(n):
    if not 0 < n < 4000:
        return b""
    vals = [(1000, b"M"), (900, b"CM"), (500, b"D"), (400, b"CD"), (100, b"C"), (90, b"XC"), (50, b"L"), (40, b"XL"),
            (10, b"X"), (9, b"IX"), (5, b"V"), (4, b"IV"), (1, b"I")]
    out = b""
    for v, s in vals:
        while n >= v:
            out += s
            n -= v
    return out


def isbn10_ok(d):
    s = 0
    for i, c in enumerate(d):
        v = 10 if c in b"Xx" else c - 48
        s += (10 - i) * v
    return s % 11 == 0


def isbn13_ok(d):
    s = sum((c - 48) * (1 if i % 2 == 0 else 3) for i, c in enumerate(d))
    return s % 10 == 0


def ordsuf(n):
    if 10 <= n % 100 <= 20:
        return b"th"
    return {1: b"st", 2: b"nd", 3: b"rd"}.get(n % 10, b"th")


NUMRE = re.compile(rb"(?<![\w.,&])(\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?|\.\d{3})(?![\w])")


def nval(b):
    return float(b.replace(b",", b""))


def ndec(b):
    _, dot, fp = b.partition(b".")
    return len(fp) if dot else 0


def is_year(b):
    return len(b) == 4 and b.isdigit() and 1000 <= int(b) <= 2039


# --------------------------------------------------------------------------- candidates
# each: fn(page, base, title, rambot) -> list of (start, end, holds) in absolute text positions

RE_ISBN = re.compile(rb"ISBN(?:-1[03])?:?\s*([0-9][0-9 \-]{8,16}[0-9Xx])(?![0-9Xx])")


def c_isbn(p, base, title, rb_, kind):
    out = []
    for m in RE_ISBN.finditer(p):
        g = m.group(1)
        d = bytes(c for c in g if c not in b" -")
        if kind == 10 and len(d) == 10 and d[:9].isdigit():
            out.append((base + m.end(1) - 1, base + m.end(1), isbn10_ok(d)))
        elif kind == 13 and len(d) == 13 and d.isdigit():
            out.append((base + m.end(1) - 1, base + m.end(1), isbn13_ok(d)))
    return out


RE_ISSN = re.compile(rb"ISSN:?\s*(\d{4})-?(\d{3})([\dXx])")


def c_issn(p, base, title, rb_):
    out = []
    for m in RE_ISSN.finditer(p):
        d = m.group(1) + m.group(2)
        s = sum((8 - i) * (c - 48) for i, c in enumerate(d))
        chk = (11 - s % 11) % 11
        want = b"X" if chk == 10 else bytes([48 + chk])
        out.append((base + m.start(3), base + m.end(3), m.group(3).upper() == want))
    return out


RE_YEARART = re.compile(rb"\[Roman numerals\|([MDCLXVI]+)\]")
RE_YSTART = re.compile(rb"was an? (common|leap) year starting on \[?(" + b"|".join(WDAYS) + rb")")


def year_of_title(title):
    return int(title) if title.isdigit() and 1 <= int(title) <= 2100 else None


def c_year_roman(p, base, title, rb_):
    y = year_of_title(title)
    if y is None:
        return []
    m = RE_YEARART.search(p, 0, 3000)
    if not m:
        return []
    return [(base + m.start(1), base + m.end(1), m.group(1) == roman(y))]


def c_year_wday(p, base, title, rb_):
    y = year_of_title(title)
    if y is None:
        return []
    m = RE_YSTART.search(p, 0, 4000)
    if not m:
        return []
    leap_g = (y % 4 == 0 and y % 100 != 0) or y % 400 == 0
    leap_j = y % 4 == 0
    wd = WDAYS.index(m.group(2))
    ok = (wd == weekday(y, 1, 1, True) and (m.group(1) == b"leap") == leap_g) or \
         (wd == weekday(y, 1, 1, False) and (m.group(1) == b"leap") == leap_j)
    return [(base + m.start(1), base + m.end(1), ok), (base + m.start(2), base + m.end(2), ok)]


RE_DOY = re.compile(rb"is the \[?(?:[^\]|\n]*\|)?(\d+)\]?(?:st|nd|rd|th) day of the year.{0,80}?There (?:are|is) (\d+) days? remaining")
RE_DOYT = re.compile(MON + rb" (\d{1,2})$")


def c_doy(p, base, title, rb_):
    t = RE_DOYT.match(title)
    m = RE_DOY.search(p, 0, 3000)
    if not t or not m:
        return []
    mo, d = MI[t.group(1)], int(t.group(2))
    if not valid_date(2001, mo, d) and not (mo == 2 and d == 29):
        return []
    n = (datetime.date(2001, mo, d) - datetime.date(2001, 1, 1)).days + 1 if valid_date(2001, mo, d) else 60
    return [(base + m.start(1), base + m.end(1), int(m.group(1)) == n),
            (base + m.start(2), base + m.end(2), int(m.group(2)) == 365 - n)]


# weekday given after the date: derivable fully
RE_WD_AFTER = re.compile(rb"\[?(?:" + MON + rb" (\d{1,2})|(\d{1,2}) " + MON + rb")\]?,? \[?(\d{4})\]?,? \(?(?:a |which was a |that was a |on a )?" + WDR)
# weekday given before the date: the date's year is constrained (about 1/7 of years fit)
RE_WD_BEFORE = re.compile(WDR + rb",? (?:the )?\[?(?:" + MON + rb" (\d{1,2})(?:st|nd|rd|th)?|(\d{1,2})(?:st|nd|rd|th)? (?:of )?" + MON + rb")\]?,? \[?(\d{4})\]?")


def _ymd(g, mo1, d1, d2, mo2, y):
    mo = MI[g[mo1] or g[mo2]]
    d = int(g[d1] or g[d2])
    return int(g[y]), mo, d


def c_wday_after(p, base, title, rb_):
    out = []
    for m in RE_WD_AFTER.finditer(p):
        g = m.groups()
        y, mo, d = _ymd(g, 0, 1, 2, 3, 4)
        if not valid_date(y, mo, d):
            continue
        wd = WDAYS.index(g[5])
        ok = wd == weekday(y, mo, d) or (y < 1753 and wd == weekday(y, mo, d, False))
        out.append((base + m.start(6), base + m.end(6), ok))
    return out


def c_wday_before(p, base, title, rb_):
    out = []
    for m in RE_WD_BEFORE.finditer(p):
        g = m.groups()
        y, mo, d = _ymd(g, 1, 2, 3, 4, 5)
        if not valid_date(y, mo, d):
            continue
        wd = WDAYS.index(g[0])
        ok = wd == weekday(y, mo, d) or (y < 1753 and wd == weekday(y, mo, d, False))
        out.append((base + m.start(6), base + m.end(6), ok))
    return out


RE_LIFE = re.compile(rb"\(([^()\n]{0,80}?)\b(\d{3,4})\]?\s*(?:\xe2\x80\x93|-|&ndash;|&&ndash;)\s*([^()\n]{0,60}?)\b(\d{3,4})\]?\s*\)")
RE_BORN = re.compile(rb"\((?:born|b\.)[^()\n]{0,60}?\b(\d{4})\]?\s*\)")
RE_AGE = re.compile(rb"(?:\baged|\bat the age of|\bat age|\bage of|\(age)\s+(\d{1,3})\b")
RE_Y4 = re.compile(rb"(?<![\d,.])(1[0-9]{3}|20[0-3][0-9])(?![\d,])")


def c_age(p, base, title, rb_):
    head = p[:3000]
    by = dy = None
    m = RE_LIFE.search(head)
    if m:
        by, dy = int(m.group(2)), int(m.group(4))
        if not (0 < dy - by < 125):
            by = dy = None
    if by is None:
        m = RE_BORN.search(head)
        if m:
            by = int(m.group(1))
    if by is None:
        return []
    out = []
    for m in RE_AGE.finditer(p):
        a = int(m.group(1))
        ls = p.rfind(b"\n", 0, m.start()) + 1
        lo = max(ls, m.start() - 300)
        ys = [int(x.group(1)) for x in RE_Y4.finditer(p, lo, m.start())]
        ys = [y for y in ys if by < y <= by + 125]
        cands = set()
        if ys:
            cands |= {ys[-1] - by, ys[-1] - by - 1}
        elif dy is not None:
            cands |= {dy - by, dy - by - 1}
        else:
            continue  # nothing to derive from
        out.append((base + m.start(1), base + m.end(1), a in cands))
    return out


RE_DUR = re.compile(rb"(?<![\d,])(\d{3,4})\]?\s*(?:\xe2\x80\x93|-|&ndash;|&&ndash;|to|until)\s*\[?(\d{2,4})\]?\)?,?\s*(?:\(|, |for |a total of |, a period of )?(?:some |about |nearly |almost |over )?(\d{1,3})(?: |-)years?")


def c_duration(p, base, title, rb_):
    out = []
    for m in RE_DUR.finditer(p):
        y1, y2s, n = int(m.group(1)), m.group(2), int(m.group(3))
        y2 = int(y2s)
        if len(y2s) == 2:
            y2 = (y1 // 100) * 100 + y2
            if y2 < y1:
                y2 += 100
        if not (0 <= y2 - y1 < 400):
            continue
        out.append((base + m.start(3), base + m.end(3), n in (y2 - y1, y2 - y1 + 1, y2 - y1 - 1)))
    return out


RE_ROM1 = re.compile(rb"\b([IVXLCDM]{2,})\b\]?\s*\((\d{1,4})\)")
RE_ROM2 = re.compile(rb"(?<![\d,.])(\d{1,4})\]?\s*\('*\[?(?:Roman numerals\|)?([IVXLCDM]{1,})\]?'*\)")


def c_roman_pair(p, base, title, rb_):
    out = []
    for m in RE_ROM1.finditer(p):
        out.append((base + m.start(2), base + m.end(2), roman(int(m.group(2))) == m.group(1)))
    for m in RE_ROM2.finditer(p):
        out.append((base + m.start(2), base + m.end(2), roman(int(m.group(1))) == m.group(2)))
    return out


RE_ORD = re.compile(rb"(?<![\d.,])(\d{1,4})(st|nd|rd|th)\b")


def c_ordinal(p, base, title, rb_):
    return [(base + m.start(2), base + m.end(2), ordsuf(int(m.group(1))) == m.group(2)) for m in RE_ORD.finditer(p)]


RE_LISTY = re.compile(rb"^(?:\*+|#+)\s*(?:'''|'')?\[?(\d{3,4})\]?(?![\d,.])", re.M)


def c_chrono(p, base, title, rb_):
    out = []
    prev = None
    prev_end = -1
    for m in RE_LISTY.finditer(p):
        y = int(m.group(1))
        ls = m.start()
        if prev is not None:
            between = p[prev_end:ls]
            # contiguous list (only list lines in between, no heading)
            if b"\n=" not in between and between.count(b"\n") <= 3:
                out.append((base + m.start(1), base + m.end(1), y >= prev))
        prev = y
        prev_end = m.end()
    return out


RE_PCT = re.compile(rb"(?<![\d.,])(\d{1,3}(?:\.\d+)?)\s?(?:%|&&#37;| percent| per cent)")


def c_sum100(p, base, title, rb_):
    if rb_:
        return []
    out = []
    for line in re.finditer(rb"[^\n]+", p):
        ps = list(RE_PCT.finditer(line.group(0)))
        if len(ps) < 3:
            continue
        vals = [float(x.group(1)) for x in ps]
        dec = max(ndec(x.group(1)) for x in ps)
        last = ps[-1]
        ok = abs(sum(vals) - 100.0) < 0.5 * 10 ** -dec
        out.append((base + line.start() + last.start(1), base + line.start() + last.end(1), ok))
    return out


RE_EB = re.compile(rb"\{(?:Template:)?Election box ([a-z ]+?)(?:\||\n)(.*?)\n\}", re.S)
RE_FIELD = re.compile(rb"\|\s*(votes|percentage)\s*=\s*([0-9][0-9,]*(?:\.\d+)?)")


def pct_set(v, Plo, Phi, dec):
    """display values of 100 v / P for P in [Plo, Phi]."""
    if Plo <= 0 or Phi < Plo:
        return None
    hi, lo = 100 * v / Plo, 100 * v / Phi
    u = 10 ** -dec
    a, b = math.floor(lo / u + 0.5), math.floor(hi / u + 0.5)
    if b - a > 50:
        return None
    return {round(k * u, dec) for k in range(a, b + 1)}


def c_election(p, base, title, rb_):
    out = []
    Plo, Phi = 0.0, 1e18
    cand_votes = []
    for m in RE_EB.finditer(p):
        kind = m.group(1)
        f = {k: (v, m.start(2) + mm.start(2), m.start(2) + mm.end(2)) for mm in RE_FIELD.finditer(m.group(2))
             for k, v in [(mm.group(1), mm.group(2))]}
        if kind.startswith(b"begin"):
            Plo, Phi, cand_votes = 0.0, 1e18, []
            continue
        if kind.startswith(b"candidate") and b"votes" in f:
            v = nval(f[b"votes"][0])
            if b"percentage" in f:
                ps, s, e = f[b"percentage"]
                pv, dec = nval(ps), ndec(ps)
                if cand_votes and Phi < 1e17:
                    st = pct_set(v, Plo, Phi, dec)
                    out.append((base + s, base + e, st is not None and round(pv, dec) in st and len(st) <= 4))
                h = 0.5 * 10 ** -dec
                if pv - h > 0 and v > 0:
                    Plo, Phi = max(Plo, 100 * v / (pv + h)), min(Phi, 100 * v / (pv - h))
            cand_votes.append(v)
        elif kind.startswith(b"majority") and b"votes" in f and len(cand_votes) >= 2:
            vs, s, e = f[b"votes"]
            top = sorted(cand_votes, reverse=True)
            out.append((base + s, base + e, nval(vs) == top[0] - top[1]))
        elif kind.startswith(b"turnout") and b"votes" in f and cand_votes:
            vs, s, e = f[b"votes"]
            out.append((base + s, base + e, nval(vs) == sum(cand_votes)))
    return out


RE_DATES = [
    (0, re.compile(rb"\[?" + MON + rb" (\d{1,2})\]?,? \[?(\d{4})(?![\d,])")),
    (1, re.compile(rb"(?<![\d,])\[?(\d{1,2}) " + MON + rb"\]?,? \[?(\d{4})(?![\d,])")),
    (2, re.compile(rb"(?<![\d-])(\d{4})-(\d{2})-(\d{2})(?![\d])")),
    (3, re.compile(rb"(?:date|date and age)\|(\d{4})\|(\d{1,2})\|(\d{1,2})")),
]


def date_mentions(p):
    ms = []
    for fmt, rx in RE_DATES:
        for m in rx.finditer(p):
            g = m.groups()
            try:
                if fmt == 0:
                    y, mo, d = int(g[2]), MI[g[0]], int(g[1])
                elif fmt == 1:
                    y, mo, d = int(g[2]), MI[g[1]], int(g[0])
                else:
                    y, mo, d = int(g[0]), int(g[1]), int(g[2])
            except (ValueError, KeyError):
                continue
            if valid_date(y, mo, d):
                ms.append((m.start(), m.end(), fmt, (y, mo, d)))
    ms.sort()
    return ms


def c_date_reformat(p, base, title, rb_, same):
    out = []
    seen = {}
    for s, e, fmt, ymd in date_mentions(p):
        if ymd in seen:
            fmts = seen[ymd]
            if same == (fmt in fmts):
                out.append((base + s, base + e, True))
            fmts.add(fmt)
        else:
            seen[ymd] = {fmt}
    return out


RE_TAXF = re.compile(rb"^\|\s*(genus|species|binomial)\s*=\s*(.*)$", re.M)


def c_binomial(p, base, title, rb_):
    if b"axobox" not in p:
        return []
    f = {}
    out = []
    for m in RE_TAXF.finditer(p):
        k = m.group(1)
        v = re.sub(rb"['\[\]]", b"", m.group(2)).strip()
        if k == b"binomial" and b"genus" in f and b"species" in f:
            g = f[b"genus"].split(b"|")[-1].strip()
            sp = f[b"species"].split(b"|")[-1].strip().split(b" ")[-1]
            out.append((base + m.start(2), base + m.end(2), v == g + b" " + sp))
        f[k] = v
    return out


RE_MM = re.compile(rb"(?:Chembox/MolarMass\||[Mm]olar ?[Mm]ass\s*=\s*|molecular_weight\s*=\s*)([0-9][0-9.,]*)")


def c_molar(p, base, title, rb_):
    return [(base + m.start(1), base + m.end(1), True) for m in RE_MM.finditer(p)]


RE_ORB = re.compile(rb"^\|\s*(?:period|orbital_period|semimajor|semi-major axis|aphelion|perihelion|eccentricity|sidereal_period)\s*=\s*([0-9][0-9.,]*)", re.M | re.I)


def c_orbit(p, base, title, rb_):
    return [(base + m.start(1), base + m.end(1), True) for m in RE_ORB.finditer(p)]


RE_DMS = re.compile(rb"\{(?:coor|coord)[^}\n]{0,120}\}|latd\s*=")


def c_coord(p, base, title, rb_):
    if rb_:
        return []
    return [(base + m.start(), base + m.end(), True) for m in RE_DMS.finditer(p)]


RE_IW = re.compile(rb"^\[([a-z][a-z\-]{1,11}):([^\]\n]+)\]$", re.M)


def iw_script(t):
    if max(t) < 0x80:
        return "ascii"
    # UTF-8 lead bytes 0xC3..0xC5 cover U+00C0..U+017F (Latin-1 supplement, Latin Extended-A)
    if all(c < 0x80 or 0x80 <= c < 0xC0 or 0xC3 <= c <= 0xC9 for c in t):
        return "latin_ext"
    return "nonlatin"


def c_interwiki_tail(tail, base, which):
    """phda9 moves the interlanguage links to a block after the last page; grouped per article, in page order."""
    out = []
    for m in RE_IW.finditer(tail):
        if iw_script(m.group(2)) == which:
            out.append((base + m.start(2), base + m.end(2), True))
    return out


# ----------------------------------------------------------- generic numeric-law miner (tables, infoboxes, prose)

FACTORS = [1.609344, 1 / 1.609344, 2.589988, 1 / 2.589988, 3.28084, 0.3048, 2.20462, 0.453592, 2.54, 1 / 2.54,
           0.0254 * 1000, 1 / 25.4, 0.404686, 2.47105, 1.8, 1 / 1.8]


def segments(p):
    """Number groups: a prose line, an infobox block (consecutive '| key = ' lines), a table row ('|-' separated)."""
    seg = 0
    segs = []
    pos = 0
    prev_kind = None
    for line in p.split(b"\n"):
        L = len(line)
        st = line[:2]
        if st.startswith(b"{|") or st.startswith(b"|-") or st.startswith(b"|}"):
            seg += 1
            kind = b"t"
        elif line.startswith(b"|") or line.startswith(b"!"):
            kind = b"t"
            if prev_kind != b"t":
                seg += 1
        else:
            kind = b"p"
            seg += 1
        segs.append((pos, pos + L, seg))
        prev_kind = kind
        pos += L + 1
    return segs


def law_check(x, xs, ctx, after):
    """Which law (if any) gives x from the context numbers ctx (list of (bytes, value)). after = text after x."""
    v = nval(x)
    dec = ndec(x)
    h = 0.5 * 10 ** -dec + 1e-9
    vals = [c[1] for c in ctx]
    nonyear = [not is_year(c[0]) for c in ctx]
    if is_year(x) and dec == 0:
        return None
    n = len(vals)
    # sums of the k numbers right before x
    if dec == 0 and v >= 3:
        for k in (2, 3, 4):
            if n >= k and all(nonyear[n - k:]) and abs(sum(vals[n - k:]) - v) < h and sum(1 for t in vals[n - k:] if t) >= 2:
                return "sum"
    # sports tables: L = Pld - W - D; Pts = 3W + D or 2W + D next to a (Pld, W, D, L) quadruple; W-L percentage
    if dec == 0 and n >= 3 and all(nonyear[n - 3:]) and vals[-3] >= 3 and abs(vals[-3] - vals[-2] - vals[-1] - v) < h \
            and vals[-2] + vals[-1] > 0:
        return "pld"
    if dec == 0 and v >= 1:
        for i in range(1, n - 2):
            P_, W, D, L = vals[i - 1], vals[i], vals[i + 1], vals[i + 2]
            if P_ >= 3 and P_ == W + D + L and (v == 3 * W + D or v == 2 * W + D) and all(nonyear[i - 1:i + 3]):
                return "pts"
    if dec == 3 and v <= 1 and n >= 2:
        for i in range(max(0, n - 4), n - 1):
            W, L = vals[i], vals[i + 1]
            if W + L > 0 and abs(W / (W + L) - v) < h:
                return "wpct"
            if i + 2 < n:
                T = vals[i + 2]
                if W + L + T > 0 and (abs((W + T / 2) / (W + L + T) - v) < h or abs(W / (W + L + T) - v) < h):
                    return "wpct"
    # differences of the two numbers right before x (goal difference, majority)
    if n >= 2 and v >= 2 and all(nonyear[n - 2:]) and abs(abs(vals[-2] - vals[-1]) - v) < h:
        return "diff"
    # durations between two years in the context
    if dec == 0 and v < 400:
        ys = [int(c[1]) for c in ctx[-4:] if is_year(c[0])]
        if len(ys) >= 2 and (ys[-1] - ys[-2]) in (v, v - 1, v + 1) and ys[-1] > ys[-2]:
            return "years"
    sig = len(x.replace(b",", b"").replace(b".", b"").lstrip(b"0"))
    pctish = dec > 0 or after.startswith(b"%") or after.startswith(b" %") or after.startswith(b" per")
    if sig >= 2 and v > 0:
        # percentage of two context numbers
        if pctish and v <= 100:
            for i in range(max(0, n - 6), n):
                for j in range(max(0, n - 6), n):
                    if i != j and nonyear[i] and nonyear[j] and 0 < vals[i] < vals[j] and vals[j] >= 10:
                        if abs(100 * vals[i] / vals[j] - v) < h:
                            return "pct"
        # unit conversions of a context number
        for i in range(max(0, n - 4), n):
            if nonyear[i] and vals[i] > 0:
                for f in FACTORS:
                    if abs(vals[i] * f - v) < h and sig >= 3 and len(ctx[i][0].replace(b",", b"").replace(b".", b"").lstrip(b"0")) >= 2:
                        return "conv"
                if abs(vals[i] * 1.8 + 32 - v) < h or abs((vals[i] - 32) / 1.8 - v) < h:
                    return "conv"
        # ratios (densities, averages)
        if sig >= 3:
            for i in range(max(0, n - 6), n):
                for j in range(max(0, n - 6), n):
                    if i != j and nonyear[i] and nonyear[j] and vals[j] > 1 and vals[i] > vals[j]:
                        if abs(vals[i] / vals[j] - v) < h:
                            return "ratio"
    return None


def c_numlaw(p, base, title, rb_, which, null=False, rng=None, pool=None, fill=None):
    if rb_:
        return []
    out = []
    segs = segments(p)
    nums = []  # (start, end, bytes, value, seg)
    si = 0
    for m in NUMRE.finditer(p):
        while si + 1 < len(segs) and segs[si][1] < m.start():
            si += 1
        b = m.group(1)
        try:
            nums.append((m.start(1), m.end(1), b, nval(b), segs[si][2]))
        except ValueError:
            pass
    if fill is not None:
        fill.extend(nums[:: max(1, len(nums) // 50 + 1)])
        return []
    for i, (s, e, b, v, sg) in enumerate(nums):
        j = i
        while j > 0 and nums[j - 1][4] == sg and i - j < 8:
            j -= 1
        if null:
            if not pool or len(pool) < 100:
                continue
            k = rng.randrange(len(pool) - 8)
            ctx = [(c[2], c[3]) for c in pool[k:k + (i - j)]]
        else:
            ctx = [(c[2], c[3]) for c in nums[j:i]]
        if not ctx:
            continue
        law = law_check(b, None, ctx, p[e:e + 4])
        if law == which:
            out.append((base + s, base + e, True))
    return out


# --------------------------------------------------------------------------- driver

CANDS = [
    # name, track, fn
    ("isbn10_check", "B", lambda *a: c_isbn(*a, 10)),
    ("isbn13_check", "B", lambda *a: c_isbn(*a, 13)),
    ("issn_check", "B", c_issn),
    ("year_roman", "B", c_year_roman),
    ("year_start_wday", "B", c_year_wday),
    ("doy_date_article", "B", c_doy),
    ("wday_after_date", "B", c_wday_after),
    ("year_after_wday", "B", c_wday_before),
    ("age_from_years", "B", c_age),
    ("duration_years", "B", c_duration),
    ("roman_arabic_pair", "B", c_roman_pair),
    ("ordinal_suffix", "B", c_ordinal),
    ("chrono_list_year", "B", c_chrono),
    ("pct_sum100_last", "B", c_sum100),
    ("election_box", "B", c_election),
    ("date_repeat_samefmt", "B", lambda *a: c_date_reformat(*a, True)),
    ("date_repeat_newfmt", "B", lambda *a: c_date_reformat(*a, False)),
    ("taxo_binomial", "B", c_binomial),
    ("chem_molar_mass", "B", c_molar),
    ("orbit_fields", "B", c_orbit),
    ("coord_templates", "B", c_coord),
    ("numlaw_sum", "B", lambda *a: c_numlaw(*a, "sum")),
    ("numlaw_diff", "B", lambda *a: c_numlaw(*a, "diff")),
    ("numlaw_years", "B", lambda *a: c_numlaw(*a, "years")),
    ("numlaw_pct", "B", lambda *a: c_numlaw(*a, "pct")),
    ("numlaw_conv", "B", lambda *a: c_numlaw(*a, "conv")),
    ("numlaw_ratio", "B", lambda *a: c_numlaw(*a, "ratio")),
    ("numlaw_pld", "B", lambda *a: c_numlaw(*a, "pld")),
    ("numlaw_pts", "B", lambda *a: c_numlaw(*a, "pts")),
    ("numlaw_wpct", "B", lambda *a: c_numlaw(*a, "wpct")),
    ("interwiki_ascii", "A", None),
    ("interwiki_latin_ext", "A", None),
    ("interwiki_nonlatin", "A", None),
]


def pages_of(text):
    st = [m.start() for m in re.finditer(rb"  <page>\n", text)]
    end = text.rfind(b"  </page>\n")
    st = [s for s in st if s < end] + [end + 10]
    return st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True)
    ap.add_argument("--cost")
    ap.add_argument("--dict")
    ap.add_argument("--out")
    ap.add_argument("--sample", help="N:MB evenly spaced chunks")
    ap.add_argument("--only", help="comma list of candidate names")
    ap.add_argument("--null", action="store_true", help="numlaw: also count with shuffled context (coincidence rate)")
    args = ap.parse_args()

    text = open(args.pre, "rb").read()
    ccum = None
    if args.cost:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import a1_analyze as a1  # noqa: E402
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
        import costlog  # noqa: E402
        cache = os.path.join("build/exp/X2", "tcost_" + os.path.basename(args.cost) + ".npy")
        if os.path.exists(cache):
            ccum = np.load(cache)
        else:
            r = costlog.load(args.cost, mmap=False)
            dec, cmap, container = a1.wrt_decode(r["byte"].tobytes(), a1.load_dict(args.dict))
            if text[:len(dec)] != dec:
                raise SystemExit("WRT decode mismatch")
            tc = np.zeros(len(dec) + 1, np.float64)
            np.add.at(tc, np.minimum(cmap, len(dec)), r["cost"].astype(np.float64))
            ccum = np.concatenate([[0.0], np.cumsum(tc)])
            np.save(cache, ccum)
        text = text[:len(ccum) - 2]
        print(f"cost-covered text {len(text):,} B, record {ccum[-1] / 8:,.0f} B", file=sys.stderr)

    starts = pages_of(text)
    scale = 1.0
    if args.sample:
        N, mb = args.sample.split(":")
        N, sz = int(N), int(float(mb) * 1e6)
        sel = set()
        arr = np.array(starts[:-1])
        for k in range(N):
            a = int(len(text) * (k + 0.5) / N)
            i0 = int(np.searchsorted(arr, a))
            i1 = int(np.searchsorted(arr, a + sz))
            sel.update(range(i0, i1))
        idx = sorted(sel)
        covered = sum(starts[i + 1] - starts[i] for i in idx)
        scale = (starts[-1] - starts[0]) / covered
        print(f"sample: {len(idx):,} of {len(starts) - 1:,} pages, {covered:,} B, scale x{scale:.3f}", file=sys.stderr)
    else:
        idx = range(len(starts) - 1)

    cands = [c for c in CANDS if not args.only or c[0] in args.only.split(",")]
    agg = {c[0]: [0, 0, 0, 0.0, 0.0, 0] for c in cands}  # inst, holds, chars(holds), bits(all), bits(holds), null holds
    rng = random.Random(1)
    pool = []
    for n_i, i in enumerate(idx):
        a, b = starts[i], starts[i + 1]
        p = text[a:b]
        tm = re.search(rb"<title>(.*?)</title>", p)
        title = tm.group(1) if tm else b""
        rb_ = b"As of the [census]" in p or b"As of the census" in p
        for name, tr, fn in cands:
            if fn is None:
                continue
            res = fn(p, a, title, rb_)
            A = agg[name]
            for s, e, ok in res:
                A[0] += 1
                if ccum is not None:
                    bits = ccum[e] - ccum[s]
                    A[3] += bits
                    if ok:
                        A[4] += bits
                if ok:
                    A[1] += 1
                    A[2] += e - s
            if args.null and name.startswith("numlaw_"):
                A[5] += len(c_numlaw(p, a, title, rb_, name[7:], True, rng, pool))
        if args.null:
            c_numlaw(p, a, title, rb_, "none", False, None, None, pool)
            if len(pool) > 200000:
                del pool[:100000]
        if n_i % 20000 == 0:
            print(f"  {n_i:,} pages", file=sys.stderr)

    # the interlanguage block after the last page (scaled like the pages when sampling)
    tail0 = starts[-1]
    tail = text[tail0:]
    for name, tr, fn in cands:
        if fn is None and name.startswith("interwiki_"):
            A = agg[name]
            for s_, e_, ok in c_interwiki_tail(tail, tail0, name[10:]):
                if args.sample and rng.random() * scale > 1.0:
                    continue
                A[0] += 1
                A[1] += 1
                A[2] += e_ - s_
                if ccum is not None:
                    A[3] += ccum[e_] - ccum[s_]
                    A[4] += ccum[e_] - ccum[s_]

    hdr = "cand\ttrack\tinst\tholds\thold%\tchars\tbits_all\tbits_holds\tbits/hold\tnull_holds\tscale\tinst_x\tholds_x"
    rows = [hdr]
    for name, tr, _ in cands:
        n, h, ch, ba, bh, nh = agg[name]
        rows.append(f"{name}\t{tr}\t{n}\t{h}\t{100 * h / max(n, 1):.1f}\t{ch}\t{ba:.0f}\t{bh:.0f}\t"
                    f"{bh / max(h, 1):.2f}\t{nh}\t{scale:.3f}\t{n * scale:.0f}\t{h * scale:.0f}")
    s = "\n".join(rows)
    print(s)
    if args.out:
        open(args.out, "w").write(s + "\n")


if __name__ == "__main__":
    main()
