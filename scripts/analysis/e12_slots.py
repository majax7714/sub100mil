#!/usr/bin/env python3
"""E12: find derivable numbers ("slots") in the census (Rambot) articles and describe what a causal arithmetic
specialist would predict for each.

Usage:
  e12_slots.py --pre SLICE.PRE [--cost COSTLOG --dict english.dic] --out SLOTS.bin [--audit AUDIT.tsv]

Without --cost (e.g. on build/exp/A1/full/full.pre), only the audit is written (slot counts, hit rates and the
specialist's own coding cost per class and denominator bucket), which is what the full-scale projection uses.
With --cost, the coded stream is WRT-decoded (A1's verified decoder, checked against SLICE.PRE) and every slot
is mapped to the coded index of its first byte; SLOTS.bin is the input of e12_sim.cpp.

Causality. A slot is the number right after a left-context trigger (e.g. "(" after "N/km&&sup2; "), plus the
byte that ends it. Its prediction uses only numbers that appear earlier in the same article (all at text
positions < the slot start) and the trigger. Digits, '.', ',', '-', '%' and most punctuation are single coded
bytes in the WRT stream, so a slot is a contiguous run of coded bytes.

Descriptor format (little endian), per slot:
  i64 coded_start (or text pos when no cost), u8 class, u8 ncomp, u8 actual_len, actual bytes,
  then ncomp components:  u8 kind, f32 weight, then
    kind 0 LIST:    u16 n, n x (u8 len, bytes, f32 w)
    kind 1 LATTICE: i64 D, i64 kmin, i64 kmax, f64 scale, u8 dec, u8 term, f64 lo, f64 hi  (values scale*k/D)
    kind 2 RATIO:   i64 D, i64 kmin, i64 kmax, f64 scale, u8 dec, u8 term, f64 lo, f64 hi  (values scale*k/(D-k))
  Strings in LIST include the terminator; LATTICE/RATIO candidates are '%.{dec}f' (no thousands separator) + term,
  each distinct string weighted by its number of k.
"""
import argparse
import math
import os
import re
import struct
import sys
from collections import defaultdict

import numpy as np

CLASSES = [
    "race%", "race%last", "hisp%", "hh_copy", "hh%", "hh%cons", "hhsize", "famsize", "age%", "age%last",
    "sexratio", "sexratio18", "area_mi", "land_km", "land_mi", "water_km", "water_mi", "water%",
    "dens_km", "dens_mi", "hdens_km", "hdens_mi", "coord", "conv",
]
CI = {c: i for i, c in enumerate(CLASSES)}
KM2 = 2.589988110336  # km^2 per mi^2

NUM = rb"[0-9][0-9,]*(?:\.[0-9]+)?"


def fnum(b):
    return float(b.replace(b",", b""))


def half_ulp(b):
    """Half a unit of the last displayed digit of a number string."""
    _, dot, fp = b.partition(b".")
    return 0.5 * 10 ** -(len(fp) if dot else 0)


def fmt(v, dec, commas):
    s = f"{v:.{dec}f}"
    if commas:
        ip, _, fp = s.partition(".")
        neg = ip.startswith("-")
        ip = ip.lstrip("-")
        ip = f"{int(ip):,}"
        s = ("-" if neg else "") + ip + ("." + fp if fp else "")
    return s.encode()


class Slot:
    __slots__ = ("pos", "cls", "actual", "comps", "D")

    def __init__(self, pos, cls, actual, comps, D=0):
        self.pos, self.cls, self.actual, self.comps, self.D = pos, cls, actual, comps, D


def comp_list(weights_by_str, w=1.0):
    return (0, w, dict(weights_by_str))


def comp_lattice(D, dec, term, lo=-1e18, hi=1e18, kmin=0, kmax=None, scale=100.0, w=1.0):
    return (1, w, (int(D), int(kmin), int(D if kmax is None else kmax), float(scale), int(dec), term, lo, hi))


def comp_ratio(D, dec, term, kmin, kmax, lo=-1e18, hi=1e18, scale=100.0, w=1.0):
    return (2, w, (int(D), int(kmin), int(kmax), float(scale), int(dec), term, lo, hi))


def range_list(lo, hi, dec, term, commas, maxn=4000, fmtfn=None):
    """Uniform over the display values v (dec decimals) with v in [lo, hi] (rounded display of a real in [lo, hi])."""
    u = 10 ** -dec
    a = math.floor(lo / u + 0.5)
    b = math.floor(hi / u + 0.5)
    if b < a or b - a + 1 > maxn:
        return None
    d = {}
    for k in range(a, b + 1):
        v = k * u
        s = (fmtfn(v) if fmtfn else fmt(v, dec, commas)) + term
        d[s] = d.get(s, 0) + 1.0
    return d


def expand(comp):
    """All candidate strings with weights (only used for the audit; the sim expands in C++ the same way)."""
    kind, w, p = comp
    if kind == 0:
        return p
    D, kmin, kmax, scale, dec, term, lo, hi = p
    k = np.arange(max(kmin, 0), kmax + 1, dtype=np.float64)
    if kind == 1:
        v = scale * k / D
    else:
        k = k[k < D]
        v = scale * k / (D - k)
    v = v[(v >= lo - 1e-9) & (v <= hi + 1e-9)]
    out = defaultdict(float)
    for x in v:
        out[b"%.*f" % (dec, x) + term] += 1.0
    return out


def lattice_count(comp):
    kind, w, p = comp
    D, kmin, kmax, scale, dec, term, lo, hi = p
    return kmax - kmin + 1


# ---------------------------------------------------------------- the census parser

RE_PPL = re.compile(rb"there (?:are|were) (" + NUM + rb") people, (" + NUM + rb") households, and (" + NUM + rb") families")
RE_RACE = re.compile(rb"The racial makeup of the [^\n.]*? is ")
RE_PCT = re.compile(rb"([0-9]+\.[0-9]+)%")
RE_HISP = re.compile(rb"races\.  ([0-9]+\.[0-9]+)% of the population are ")
RE_HHC = re.compile(rb"\n\nThere (?:are|were) (" + NUM + rb") households out of which ")
RE_HHS = re.compile(rb"The average household size is (" + NUM + rb") and the average family size is (" + NUM + rb")")
RE_AGE = re.compile(rb"the population is spread out with ")
RE_SEX = re.compile(rb"For every 100 females there (?:are|were) (" + NUM + rb") males\.")
RE_SEX18 = re.compile(rb"For every 100 females age 18 and over, there (?:are|were) (" + NUM + rb") males\.")
RE_AREA = re.compile(rb"has a total area of (" + NUM + rb") \[square kilometer\|km&&sup2;\] \((" + NUM + rb") \[square mile\|mi&&sup2;\]\)\.")
RE_LAND = re.compile(rb"\)\.  (" + NUM + rb") km&&sup2; \((" + NUM + rb") mi&&sup2;\) of it is land")
RE_WATER = re.compile(rb" of it is land and (" + NUM + rb") km&&sup2; \((" + NUM + rb") mi&&sup2;\) of it is water")
RE_WPCT = re.compile(rb"The total area is (" + NUM + rb")% water")
RE_DENS = re.compile(rb"The \[population density\] is (" + NUM + rb")/km&&sup2; \((" + NUM + rb")/mi&&sup2;\)")
RE_HDENS = re.compile(rb"housing units at an average density of (" + NUM + rb")/km&&sup2; \((" + NUM + rb")/mi&&sup2;\)")
RE_HUNITS = re.compile(rb"There (?:are|were) (" + NUM + rb") housing units at an average density")
RE_COORD = re.compile(rb"is located at ([0-9]+)&&\xc2\xb0([0-9]+)'([0-9]+)\" (North|South), ([0-9]+)&&\xc2\xb0([0-9]+)'([0-9]+)\" (West|East) \(")
RE_DEC = re.compile(rb"-?[0-9]+\.[0-9]+")


def mi_str(v):
    """Rambot's mi^2 formatting: 1 decimal, or 2 decimals when that would print 0.0."""
    if round(v, 1) < 0.1 and v > 0:
        return f"{v:.2f}".encode()
    return f"{v:.1f}".encode()


def km_to_mi_list(A, term, slack=0.0):
    """Candidates for the mi^2 display of an area whose km^2 display is A (km = round(x*KM2, 1))."""
    lo = max((A - 0.05 - slack) / KM2, 0.0)
    hi = (A + 0.05 + slack) / KM2
    d = defaultdict(float)
    n = 400
    for i in range(n):
        x = lo + (hi - lo) * (i + 0.5) / n
        d[mi_str(x) + term] += 1.0 / n
    return d


def interval_and(*ivs):
    lo = max(a for a, b in ivs)
    hi = min(b for a, b in ivs)
    if lo > hi:  # inconsistent: fall back to the union hull of the first one
        return ivs[0]
    return lo, hi


def num_after(a, pos):
    m = re.compile(NUM).match(a, pos)
    return m.group(0) if m else b""


def census_slots(a, base, out):
    """Append the slots of one article a (bytes), text offset base."""
    m = RE_PPL.search(a)
    st = {}
    # ---- geography (comes before the demographics)
    g = RE_AREA.search(a)
    if g:
        A = fnum(g.group(1))
        out.append(Slot(base + g.start(2), CI["area_mi"], g.group(2) + b" ", [comp_list(km_to_mi_list(A, b" "))]))
        B = fnum(g.group(2))
        st["T"] = interval_and((A - 0.05, A + 0.05), ((B - 0.05) * KM2, (B + 0.05) * KM2))
        st["A"], st["B"] = A, B
        la = RE_LAND.match(a, g.end() - 2)
        if la:
            C, Dm = fnum(la.group(1)), fnum(la.group(2))
            d = {fmt(A, 1, False) + b" ": 1.0}
            rest = range_list(0.0, A - 0.1, 1, b" ", False)
            comps = [comp_list(d, 0.7)]
            if rest:
                comps.append(comp_list(rest, 0.3))
            out.append(Slot(base + la.start(1), CI["land_km"], la.group(1) + b" ", comps))
            out.append(Slot(base + la.start(2), CI["land_mi"], la.group(2) + b" ", [comp_list(km_to_mi_list(C, b" "))]))
            ld = half_ulp(la.group(2))
            st["L"] = interval_and((C - 0.05, C + 0.05), ((Dm - ld) * KM2, (Dm + ld) * KM2))
            st["Lmi"] = interval_and(((C - 0.05) / KM2, (C + 0.05) / KM2), (Dm - ld, Dm + ld))
            w = RE_WATER.match(a, la.end() - len(b" of it is land"))
            if w:
                E, Fw = fnum(w.group(1)), fnum(w.group(2))
                diff = A - C
                d = {}
                for k, wt in ((-1, 0.25), (0, 0.5), (1, 0.25)):
                    v = round(diff + 0.1 * k, 1)
                    if v >= 0:
                        d[fmt(v, 1, False) + b" "] = d.get(fmt(v, 1, False) + b" ", 0) + wt
                out.append(Slot(base + w.start(1), CI["water_km"], w.group(1) + b" ", [comp_list(d)]))
                c1 = km_to_mi_list(E, b" ")
                out.append(Slot(base + w.start(2), CI["water_mi"], w.group(2) + b" ", [comp_list(c1)]))
                fd = half_ulp(w.group(2))
                Wi = interval_and((E - 0.05, E + 0.05), ((Fw - fd) * KM2, (Fw + fd) * KM2),
                                  (st["T"][0] - st["L"][1], st["T"][1] - st["L"][0]))
                st["W"] = Wi
                wp = RE_WPCT.search(a, w.end())
                if wp and wp.start() - w.end() < 10:
                    T = st["T"]
                    lo = 100 * max(Wi[0], 0) / T[1]
                    hi = 100 * Wi[1] / max(T[0], 1e-6)
                    rl = range_list(lo, hi, 2, b"%", False)
                    if rl:
                        out.append(Slot(base + wp.start(1), CI["water%"], wp.group(1) + b"%", [comp_list(rl)]))
    if not m:
        return
    P, H, F = (int(fnum(x)) for x in m.groups())
    Ps, Hs = m.group(1), m.group(2)
    # ---- densities
    dn = RE_DENS.search(a, m.end())
    if dn and P > 0 and "L" in st:
        L = st["L"]
        lo, hi = P / L[1], P / max(L[0], 1e-6)
        sl = 0.007 * (lo + hi) / 2 + 0.06
        rl = range_list(lo - sl, hi + sl, 1, b"/", True)
        if rl:
            out.append(Slot(base + dn.start(1), CI["dens_km"], dn.group(1) + b"/", [comp_list(rl)]))
        X = fnum(dn.group(1))
        Lm = st["Lmi"]
        lo2, hi2 = P / Lm[1], P / max(Lm[0], 1e-6)
        c = X * KM2
        sl2 = 0.007 * c + 0.2
        lo3, hi3 = max(lo2, c - sl2), min(hi2, c + sl2)
        if lo3 > hi3:
            lo3, hi3 = c - sl2, c + sl2
        rl = range_list(lo3, hi3, 1, b"/", True)
        if rl:
            out.append(Slot(base + dn.start(2), CI["dens_mi"], dn.group(2) + b"/", [comp_list(rl)]))
        Y = fnum(dn.group(2))
        hd = RE_HDENS.search(a, dn.end())
        hu = RE_HUNITS.search(a, dn.end())
        if hd and hu and hu.end() <= hd.start(1):
            U = fnum(hu.group(1))
            r = U / P
            lo, hi = (X - 0.05) * r, (X + 0.05) * r
            rl = range_list(lo - 1e-9, hi + 1e-9, 1, b"/", True)
            if rl:
                out.append(Slot(base + hd.start(1), CI["hdens_km"], hd.group(1) + b"/", [comp_list(rl)]))
            lo, hi = (Y - 0.05) * r, (Y + 0.05) * r
            rl = range_list(lo - 1e-9, hi + 1e-9, 1, b"/", True)
            if rl:
                out.append(Slot(base + hd.start(2), CI["hdens_mi"], hd.group(2) + b"/", [comp_list(rl)]))
    # ---- race
    r = RE_RACE.search(a, m.end())
    if r and P > 0:
        end = a.find(b"races.", r.end())
        if end > 0:
            sent = a[r.end():end]
            pcts = list(RE_PCT.finditer(sent))
            tot = 0.0
            for i, p in enumerate(pcts):
                v = fnum(p.group(1))
                pos = r.end() + p.start(1)
                last = sent[max(0, p.start(1) - 4):p.start(1)] == b"and "
                if last:
                    n = i + 1
                    tgt = 100.0 - tot
                    comps = [comp_lattice(P, 2, b"%", tgt - 0.005 * n - 1e-9, tgt + 0.005 * n + 1e-9, w=0.9),
                             comp_lattice(P, 2, b"%", w=0.1)]
                    out.append(Slot(base + pos, CI["race%last"], p.group(1) + b"%", comps, P))
                else:
                    out.append(Slot(base + pos, CI["race%"], p.group(1) + b"%",
                                    [comp_lattice(P, 2, b"%", 0, 100.0 - tot + 0.005 * (i + 1))], P))
                tot += v
            h = RE_HISP.match(a, r.end() + len(sent))
            if h:
                out.append(Slot(base + h.start(1), CI["hisp%"], h.group(1) + b"%", [comp_lattice(P, 2, b"%")], P))
    # ---- households
    hc = RE_HHC.search(a, m.end())
    if hc and H > 0:
        out.append(Slot(base + hc.start(1), CI["hh_copy"], hc.group(1) + b" ", [comp_list({Hs + b" ": 1.0})]))
        end = a.find(b"older.", hc.end())
        if end > 0:
            sent = a[hc.end():end]
            vals = []
            for i, p in enumerate(RE_PCT.finditer(sent)):
                pos = hc.end() + p.start(1)
                v = fnum(p.group(1))
                if i == 3 and len(vals) == 3:  # non-families <= 100 - married - female householder
                    hi = 100.0 - vals[1] - vals[2] + 0.1
                    comps = [comp_lattice(H, 1, b"%", 0, hi, w=0.95), comp_lattice(H, 1, b"%", w=0.05)]
                    out.append(Slot(base + pos, CI["hh%cons"], p.group(1) + b"%", comps, H))
                elif i == 5 and len(vals) == 5:  # living alone 65+ <= individuals
                    comps = [comp_lattice(H, 1, b"%", 0, vals[4] + 1e-9, w=0.95), comp_lattice(H, 1, b"%", w=0.05)]
                    out.append(Slot(base + pos, CI["hh%cons"], p.group(1) + b"%", comps, H))
                else:
                    out.append(Slot(base + pos, CI["hh%"], p.group(1) + b"%", [comp_lattice(H, 1, b"%")], H))
                vals.append(v)
        hs = RE_HHS.search(a, hc.end())
        if hs:
            # household size = household population / H, household population <= P (= P without group quarters)
            comps = [comp_lattice(H, 2, b" ", kmin=P, kmax=P, scale=1.0, w=0.5),
                     comp_lattice(H, 2, b" ", kmin=max(H, int(P * 0.5)), kmax=P, scale=1.0, w=0.5)]
            out.append(Slot(base + hs.start(1), CI["hhsize"], hs.group(1) + b" ", comps, H))
            hsz = fnum(hs.group(1))
            if F > 0:
                hpop_hi = min(P, int((hsz + 0.005) * H) + 1)
                comps = [comp_lattice(F, 2, b".", kmin=2 * F, kmax=hpop_hi, scale=1.0)]
                out.append(Slot(base + hs.start(2), CI["famsize"], hs.group(2) + b".", comps, F))
    # ---- age
    ag = RE_AGE.search(a, m.end())
    if ag and P > 0:
        end = a.find(b"older.", ag.end())
        if end > 0:
            sent = a[ag.end():end]
            tot = 0.0
            ps = list(RE_PCT.finditer(sent))
            for i, p in enumerate(ps):
                pos = ag.end() + p.start(1)
                v = fnum(p.group(1))
                last = sent[max(0, p.start(1) - 4):p.start(1)] == b"and "
                if last:
                    n = i + 1
                    tgt = 100.0 - tot
                    comps = [comp_lattice(P, 1, b"%", tgt - 0.05 * n - 1e-9, tgt + 0.05 * n + 1e-9, w=0.9),
                             comp_lattice(P, 1, b"%", w=0.1)]
                    out.append(Slot(base + pos, CI["age%last"], p.group(1) + b"%", comps, P))
                else:
                    out.append(Slot(base + pos, CI["age%"], p.group(1) + b"%",
                                    [comp_lattice(P, 1, b"%", 0, 100.0 - tot + 0.05 * (i + 1))], P))
                tot += v
                if i == 0:
                    st["u18"] = v
    # ---- sex ratios
    sx = RE_SEX.search(a, m.end())
    if sx and P > 1:
        out.append(Slot(base + sx.start(1), CI["sexratio"], sx.group(1) + b" ",
                        [comp_ratio(P, 1, b" ", int(P * 0.2), int(P * 0.8))], P))
        s18 = RE_SEX18.search(a, sx.end())
        if s18 and "u18" in st:
            u = st["u18"]
            ks = [k for k in range(max(0, int(u * P / 100) - 3), int(u * P / 100) + 4)
                  if abs(round(100 * k / P, 1) - u) < 1e-9]
            comps = []
            for k in ks:
                Aad = P - k
                if Aad > 1:
                    comps.append(comp_ratio(Aad, 1, b" ", int(Aad * 0.2), int(Aad * 0.8), w=1.0 / len(ks)))
            if comps:
                out.append(Slot(base + s18.start(1), CI["sexratio18"], s18.group(1) + b" ", comps, P))


def coord_slots(a, base, out):
    for c in RE_COORD.finditer(a):
        d1, m1, s1, ns, d2, m2, s2, ew = c.groups()
        pos = c.end()
        for (d, mm, ss, neg, term) in ((d1, m1, s1, ns == b"South", b","), (d2, m2, s2, ew == b"West", b")")):
            v = int(d) + int(mm) / 60 + int(ss) / 3600
            lo, hi = v - 0.5 / 3600 - 2e-6, v + 0.5 / 3600 + 2e-6
            dm = RE_DEC.match(a, pos)
            if not dm:
                break
            actual = dm.group(0) + a[dm.end():dm.end() + 1]
            sign = b"-" if neg else b""
            rl = range_list(lo, hi, 6, term, False, fmtfn=lambda x, sign=sign: sign + f"{x:.6f}".encode())
            if rl:
                out.append(Slot(base + pos, CI["coord"], actual, [comp_list(rl)]))
            pos = dm.end() + 2  # ", "
            if term == b",":
                if a[dm.end():dm.end() + 2] != b", ":
                    break


# ---------------------------------------------------------------- general unit conversions: "N unit (M unit2)"

FT, MI, LB, INCH = 0.3048, 1.609344, 0.45359237, 2.54
CONV = {  # source unit -> [(factor, offset)] of the likely target units
    b"km": [(1 / MI, 0)], b"kilometers": [(1 / MI, 0)], b"kilometres": [(1 / MI, 0)],
    b"mi": [(MI, 0)], b"miles": [(MI, 0)], b"mile": [(MI, 0)],
    b"m": [(1 / FT, 0)], b"metres": [(1 / FT, 0)], b"meters": [(1 / FT, 0)], b"metre": [(1 / FT, 0)], b"meter": [(1 / FT, 0)],
    b"ft": [(FT, 0)], b"feet": [(FT, 0)], b"foot": [(FT, 0)],
    b"kg": [(1 / LB, 0)], b"lb": [(LB, 0)], b"lbs": [(LB, 0)], b"pounds": [(LB, 0)],
    b"cm": [(1 / INCH, 0)], b"mm": [(0.1 / INCH, 0)], b"in": [(INCH, 0)], b"inches": [(INCH, 0)],
    b"km/h": [(1 / MI, 0)], b"mph": [(MI, 0)],
    b"acres": [(0.40468564224, 0), (0.0040468564224, 0), (4046.8564224, 0)], b"hectares": [(1 / 0.40468564224, 0)],
    b"ha": [(1 / 0.40468564224, 0)],
    b"km&&sup2;": [(1 / KM2, 0)], b"mi&&sup2;": [(KM2, 0)], b"km\xc2\xb2": [(1 / KM2, 0)], b"sq mi": [(KM2, 0)],
    b"\xc2\xb0C": [(1.8, 32), (1.8, 0)], b"&&\xc2\xb0C": [(1.8, 32), (1.8, 0)],
    b"\xc2\xb0F": [(1 / 1.8, -32 / 1.8), (1 / 1.8, 0)], b"&&\xc2\xb0F": [(1 / 1.8, -32 / 1.8), (1 / 1.8, 0)],
    b"yards": [(0.9144, 0)], b"yd": [(0.9144, 0)], b"kW": [(1.34102, 0)], b"hp": [(0.7457, 0)],
    b"t": [(1.10231, 0), (0.984207, 0)], b"tonnes": [(1.10231, 0), (0.984207, 0)], b"tons": [(0.907185, 0), (1.016047, 0)],
}
RE_CONV = re.compile(rb"(?<![0-9.,])(-?" + NUM + rb") ?(" + b"|".join(re.escape(u) for u in sorted(CONV, key=len, reverse=True))
                     + rb")\.? \(")


def conv_candidates(N, targets, commas):
    d = defaultdict(float)
    for f, o in targets:
        x = N * f + o
        vals = set()
        if x == 0:
            vals.add(0.0)
        else:
            mag = math.floor(math.log10(abs(x)))
            for sig in (1, 2, 3, 4):
                q = 10 ** (mag - sig + 1)
                vals.add(round(round(x / q) * q, 6))
            for q in (5, 50, 500):
                if abs(x) >= q:
                    vals.add(float(round(x / q) * q))
            for dec in (0, 1, 2):
                vals.add(round(x, dec))
        strs = set()
        for v in vals:
            for dec in (0, 1, 2, 3):
                if abs(round(v, dec) - v) < 1e-9:
                    break
            for cm in ((True, False) if abs(v) >= 1000 else (False,)):
                strs.add(fmt(v, dec, cm))
        for st in strs:
            for term, w in ((b" ", 0.8), (b")", 0.2)):
                d[st + term] += w / len(strs) / len(targets)
    return d


def conv_slots(text, out):
    for m in RE_CONV.finditer(text):
        try:
            N = fnum(m.group(1))
        except ValueError:
            continue
        nm = re.compile(rb"-?" + NUM).match(text, m.end())
        actual = nm.group(0) + text[nm.end():nm.end() + 1] if nm else None
        out.append(Slot(m.end(), CI["conv"], actual, [comp_list(conv_candidates(N, CONV[m.group(2)], b"," in m.group(1)))]))


def find_slots(text):
    out = []
    starts = [mm.start() for mm in re.finditer(rb"<page>", text)] + [len(text)]
    starts = [0] + starts
    for i in range(len(starts) - 1):
        a = text[starts[i]:starts[i + 1]]
        for fn, key in ((census_slots, b"census"), (coord_slots, b"is located at ")):
            if key in a:
                part = []
                try:
                    fn(a, starts[i], part)
                except (ValueError, IndexError, ZeroDivisionError, OverflowError) as e:
                    print(f"page at {starts[i]}: {fn.__name__} skipped ({e!r})", file=sys.stderr)
                    continue
                out.extend(part)
    have = {s.pos for s in out}
    conv = []
    conv_slots(text, conv)
    out.extend(s for s in conv if s.pos not in have)
    out.sort(key=lambda s: s.pos)
    return out


# ---------------------------------------------------------------- audit (specialist alone)

def dbucket(D):
    if D <= 0:
        return 0
    return min(int(math.log2(D)), 24)


def audit(slots, path):
    agg = defaultdict(lambda: [0, 0, 0.0, 0])  # n, hit, bits (spec alone, misses at 24 bits), chars
    for s in slots:
        tot = 0.0
        hitw = 0.0
        for comp in s.comps:
            cand = expand(comp)
            ws = sum(cand.values())
            if ws <= 0:
                continue
            tot += comp[1]
            hitw += comp[1] * cand.get(s.actual, 0.0) / ws if s.actual else 0.0
        p = hitw / tot if tot > 0 else 0
        key = (CLASSES[s.cls], dbucket(s.D))
        a = agg[key]
        a[0] += 1
        a[1] += p > 0
        a[2] += -math.log2(p) if p > 0 else 24.0
        a[3] += len(s.actual) if s.actual else 1
    with open(path, "w") as f:
        f.write("class\tdbucket\tslots\thit\tspec_bits\tchars\n")
        for (c, b), (n, h, bits, ch) in sorted(agg.items()):
            f.write(f"{c}\t{b}\t{n}\t{h}\t{bits:.1f}\t{ch}\n")
    bycls = defaultdict(lambda: [0, 0, 0.0, 0])
    for (c, b), v in agg.items():
        for i in range(4):
            bycls[c][i] += v[i]
    print(f"{'class':12s} {'slots':>8s} {'hit%':>7s} {'specbits/slot':>14s} {'chars/slot':>10s}")
    for c in CLASSES:
        if c in bycls:
            n, h, bits, ch = bycls[c]
            print(f"{c:12s} {n:8d} {100 * h / n:7.2f} {bits / n:14.2f} {ch / n:10.2f}")


# ---------------------------------------------------------------- write descriptors

def write_slots(slots, path, cpos):
    with open(path, "wb") as f:
        f.write(struct.pack("<q", len(slots)))
        for s in slots:
            f.write(struct.pack("<qBBB", int(cpos(s.pos)), s.cls, len(s.comps), len(s.actual)) + s.actual)
            f.write(struct.pack("<i", s.D))
            for kind, w, p in s.comps:
                f.write(struct.pack("<Bf", kind, w))
                if kind == 0:
                    items = list(p.items())
                    f.write(struct.pack("<H", len(items)))
                    for st, wt in items:
                        f.write(struct.pack("<B", len(st)) + st + struct.pack("<f", wt))
                else:
                    D, kmin, kmax, scale, dec, term, lo, hi = p
                    f.write(struct.pack("<qqqdBBdd", D, kmin, kmax, scale, dec, term[0], lo, hi))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True)
    ap.add_argument("--cost")
    ap.add_argument("--dict")
    ap.add_argument("--out")
    ap.add_argument("--audit")
    args = ap.parse_args()
    text = open(args.pre, "rb").read()
    slots = find_slots(text)
    print(f"{len(slots):,} slots")
    if args.audit:
        audit(slots, args.audit)
    if args.cost:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import a1_analyze as a1  # noqa: E402
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
        import costlog  # noqa: E402
        r = costlog.load(args.cost, mmap=False)
        stream = r["byte"].tobytes()
        dec, cmap, container = a1.wrt_decode(stream, a1.load_dict(args.dict))
        if dec != text:
            raise SystemExit("WRT decode mismatch")
        # first coded index producing each text position
        first = np.full(len(text) + 1, -1, np.int64)
        idx = np.arange(len(cmap))
        ok = ~container
        rev_pos = cmap[ok][::-1]
        rev_idx = idx[ok][::-1]
        first[rev_pos] = rev_idx
        bad = 0
        for s in slots:
            ci = first[s.pos]
            if s.actual is None:  # trigger fired but no number follows: the specialist predicts the next coded byte
                s.actual = stream[ci:ci + 1]
            elif stream[ci:ci + len(s.actual)] != s.actual and stream[ci:ci + len(s.actual) - 1] == s.actual[:-1]:
                s.actual = stream[ci:ci + len(s.actual)]  # the terminator is a word (dictionary code) in the coded stream
            if ci < 0 or stream[ci:ci + len(s.actual)] != s.actual:
                bad += 1
        print(f"slots whose coded bytes differ from the text: {bad}")
        write_slots(slots, args.out, lambda p: first[p])
    elif args.out:
        write_slots(slots, args.out, lambda p: p)


if __name__ == "__main__":
    main()
