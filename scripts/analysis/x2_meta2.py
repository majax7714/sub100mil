#!/usr/bin/env python3
"""X2: the metadata-block specialist on the FULL RUN's (reordered) block: two calibration laws.

  pid  The block stores each page id as a delta to the previous record's (">-31"). With -e's reorder the deltas
       jump. Page ids grow with the article number of the order file, so the id lies strictly between the ids of
       the nearest known article numbers below and above (earlier records of the block); candidates are the ids
       in that open interval, Laplace-weighted around the linear interpolation, window <= +-maxw.
       NOT USABLE AS IS: archive9 does not carry the order file (selfextract_decomp unpacks only the dictionary,
       the weights and the payload; sort() restores the order by page id). The page-id deltas ARE the channel that
       transmits the permutation, so this law would need the 201,077-byte order file in archive9.
  ts   Revision ids above 18.1M are issued in time order: the time is interpolated between the nearest known
       revision ids below and above (earlier records), Laplace weights, adaptive scale.
  rpid The redirects follow all articles, in id order, so a redirect's page id is most likely one of the next ids
       after the previous record's that no earlier record used (all article ids are known by then): candidates are
       the next 64 unused ids, weighted (1 + j)^-1.5. Decoder-valid (uses decoded records only).
  uid  The user id is the one last seen with this user name (a warm CM already does this; reported, not counted).

Usage:
  # specialist alone on the whole reordered block (ideal code lengths by block decile)
  python3 -P -E scripts/analysis/x2_meta2.py --meta build/exp/X2/meta_pages.tsv --report
  # slots (E12 format, x2_sim classes revid/ts/uid = 5/6/7, pid uses class 5) for a proxy's cost dump
  python3 -P -E scripts/analysis/x2_meta2.py --meta ... --pre PROXY.pre --cost COST.bin --dict english.dic --out SLOTS.bin
Timestamp text: "0" + (year - 2001) + (31 (month - 1) + day - 1) + ":" + seconds of the day.
"""
import argparse
import bisect
import datetime
import math
import os
import re
import struct
import sys
from collections import defaultdict

HI = 18_100_000
RE_REC = re.compile(rb">(-?\d+)\nrevision>\n(?:restrictions>[^\n]*\n)?id>(\d+)\ntimestamp>(0\d+:\d+)\ncontributor>\n"
                    rb"(?:username>([^\n]*)\nid>(\d+)|ip>([^\n]*))\n/contributor>\n")


def T_of_unix(u):
    d = datetime.datetime.fromtimestamp(u, datetime.timezone.utc)
    return ((d.year - 2001) * 372 + 31 * (d.month - 1) + d.day - 1) * 86400 + d.hour * 3600 + d.minute * 60 + d.second


def ts_str(T):
    days, s = divmod(T, 86400)
    y, d = divmod(days, 372)
    return b"0%d%d:%d" % (y, d, s)


class Calib:
    """Sorted known (key -> value) pairs, interpolation, adaptive Laplace scale per gap bucket."""

    def __init__(self, init=8.0):
        self.k, self.v = [], []
        self.sc = defaultdict(lambda: [init, 1.0])

    def predict(self, key):
        i = bisect.bisect_left(self.k, key)
        if 0 < i < len(self.k):
            k0, k1, v0, v1 = self.k[i - 1], self.k[i], self.v[i - 1], self.v[i]
            p = v0 + (v1 - v0) * (key - k0) / (k1 - k0)
            gap = k1 - k0
            lo, hi = v0, v1
        elif self.k:
            j = min(max(i - 1, 0), len(self.k) - 1)
            return None
        else:
            return None
        gb = min(int(math.log2(gap + 1)), 30)
        s = self.sc[gb]
        return p, max(0.7, s[0] / s[1]), gb, lo, hi

    def update(self, key, val, pred):
        if pred is not None:
            s = self.sc[pred[2]]
            s[0] += min(abs(val - pred[0]), 30000)
            s[1] += 1
        i = bisect.bisect_left(self.k, key)
        self.k.insert(i, key)
        self.v.insert(i, val)


def laplace_cands(p, b, lo, hi, maxw, wmul=8.0):
    W = int(min(maxw, max(10, wmul * b)))
    a, z = max(lo, int(math.floor(p)) - W), min(hi, int(math.ceil(p)) + W)
    return [(x, math.exp(-abs(x - p) / b)) for x in range(a, z + 1)]


def walk(meta, maxw):
    """Causal pass over the reordered block. Yields (rank, row, predictions)."""
    cal_pid = Calib(2.0)   # article number -> page id
    cal_ts = Calib(8.0)    # revision id -> time
    uids = {}
    used = set()
    prev_pid = 0
    for row in meta:
        rank, pid, rev, T, user, uid, art = row
        pr = {"prev_pid": prev_pid}
        if art >= 0:
            q = cal_pid.predict(art)
            if q is not None:
                p, b, gb, lo, hi = q
                pr["pid"] = (laplace_cands(p, b, lo + 1, hi - 1, maxw), q)
        if art < 0 and prev_pid > 0:
            c, x = [], prev_pid + 1
            while len(c) < 64:
                if x not in used:
                    c.append((x, (1.0 + len(c)) ** -1.5))
                x += 1
            pr["rpid"] = (c, None)
        if rev >= HI:
            q = cal_ts.predict(rev)
            if q is not None:
                p, b, gb, lo, hi = q
                pr["ts"] = (laplace_cands(p, b, lo, hi, maxw), q)
        if user and not user.startswith("ip:") and user in uids:
            pr["uid"] = uids[user]
        yield rank, row, pr
        if art >= 0:
            cal_pid.update(art, pid, pr["pid"][1] if "pid" in pr else None)
        if rev >= HI:
            cal_ts.update(rev, T, pr["ts"][1] if "ts" in pr else None)
        if user and not user.startswith("ip:"):
            uids[user] = uid
        used.add(pid)
        prev_pid = pid


def load_meta(path):
    rows = []
    with open(path) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            rows.append((int(c[0]), int(c[2]), int(c[3]), T_of_unix(int(c[4])), c[5], c[6], int(c[8])))
    return rows


def ideal(cands, x):
    Z = sum(w for _, w in cands)
    for v, w in cands:
        if v == x:
            return -math.log2(w / Z)
    return None


def report(meta, maxw):
    N = len(meta)
    agg = defaultdict(lambda: [0, 0, 0.0])
    for rank, row, pr in walk(meta, maxw):
        _, pid, rev, T, user, uid, art = row
        d = min(9, rank * 10 // N)
        for f, x in (("pid", pid), ("ts", T)):
            if f in pr:
                b = ideal(pr[f][0], x)
                A = agg[(f, d)]
                A[0] += 1
                if b is not None:
                    A[1] += 1
                    A[2] += b
    for f in ("pid", "ts"):
        tot_n = tot_h = tot_b = 0
        for d in range(10):
            n, h, b = agg[(f, d)]
            tot_n += n; tot_h += h; tot_b += b
            print(f"{f:4s} decile {d}: {n:6d} slots, in window {100 * h / max(n, 1):5.1f}%, specialist alone "
                  f"{b / max(h, 1):5.2f} bits/hit")
        print(f"{f:4s} all: {tot_n:,} slots, {tot_h:,} in window, {tot_b / max(tot_h, 1):.2f} bits/hit")


def slots(meta, args):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import numpy as np
    import a1_analyze as a1  # noqa: E402
    import x2_census as xc  # noqa: E402
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
    import costlog  # noqa: E402
    enc = {a1.SWAP[s]: s for s in range(256)}

    def encode(s):
        return bytes(enc[c] for c in s)

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

    def cpos(p):
        c = first[p]
        return c if c >= 0 and cmap[c] == p else -1

    e = xc.pages_of(text)[-1]
    recs = {int(m.group(2)): m for m in RE_REC.finditer(text, e)}
    out = []
    stat = defaultdict(lambda: [0, 0])
    first_rank = None
    for rank, row, pr in walk(meta, args.maxw):
        _, pid, rev, T, user, uid, art = row
        m = recs.get(rev)
        if m is None:
            continue
        if first_rank is None:
            first_rank = rank
            continue  # the proxy's first delta is relative to 0, not to the previous page of the full block
        for f, cls, grp, cand_fn in (
                ("pid", 5, 1, lambda v: b"%d\n" % (v - pr["prev_pid"])),
                ("rpid", 5, 1, lambda v: b"%d\n" % (v - pr["prev_pid"])),
                ("ts", 6, 3, lambda v: ts_str(v) + b"\n"),
                ("uid", 7, 5, None)):
            if f not in pr or f not in args.fields:
                continue
            c = cpos(m.start(grp))
            if c < 0:
                continue
            L = m.end(grp) - m.start(grp) + 1
            act = stream[c:c + L]
            if f == "uid":
                cands = [(encode(pr["uid"].encode() + b"\n"), 1.0)]
            else:
                cands = [(encode(cand_fn(v)), w) for v, w in pr[f][0]]
            stat[f][0] += 1
            stat[f][1] += any(s == act for s, _ in cands)
            out.append((c, cls, act, cands))
    out.sort(key=lambda s: s[0])
    print(f"proxy block: {len(recs):,} records, first full-block rank {first_rank:,}; slots per field (n, actual among "
          f"candidates): {dict(stat)}")
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
                f.write(struct.pack("<B", len(s)) + s + struct.pack("<f", max(w, 1e-30)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--meta", required=True)
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--pre")
    ap.add_argument("--cost")
    ap.add_argument("--dict")
    ap.add_argument("--out")
    ap.add_argument("--fields", default="pid,ts,uid", help="pid,rpid,ts,uid")
    ap.add_argument("--maxw", type=int, default=2000)
    args = ap.parse_args()
    meta = load_meta(args.meta)
    if args.report:
        report(meta, args.maxw)
    if args.out:
        slots(meta, args)


if __name__ == "__main__":
    main()
