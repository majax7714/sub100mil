#!/usr/bin/env python3
"""X2: causal slots for the metadata-block specialist ("stratigraphy / radiometric dating"), E12 slot format.

Usage:
  python3 -P -E scripts/analysis/x2_meta_slots.py --pre SLICE.PRE --cost COSTLOG --dict english.dic --out SLOTS.bin
         [--full build/exp/A1/full/full.pre] [--fields revid,ts,uid] [--report]

phda9 moves every page's revision id, timestamp and contributor into one block after the last page. Three laws:
  revid  Revisions 14.0M..18.1M were renumbered in page order (2005 import): such a page's id is the previous such
         id + a small delta (1: 43%, 2: 19%, 3: 11%). Candidates: last chain id + d, d = 1..64, weighted by an adaptive
         histogram of d, total weight = adaptive chain rate.
  ts     Above 18.1M, revision ids are issued in time order. Candidates: times around the linear interpolation between
         the nearest known ids below and above (all earlier records of the block), Laplace weights with an adaptive
         scale per neighbour-gap bucket, window <= +-2,000 s (--maxw). Abstains in the renumbered zone.
  uid    A user's id is a function of the user name: the id last seen with this name (deterministic).
The specialist's knowledge when it reaches a record is exactly a full run's: all earlier records of the FULL block
(the slice's records are located in the full block by revision id). Slots open right after "id>" / "timestamp>"
(left-context triggers) and include the terminating newline.
Timestamp format: "0" + (year - 2001) + (31 (month - 1) + day - 1) + ":" + seconds of the day, unpadded.
"""
import argparse
import bisect
import math
import os
import re
import struct
import sys
from collections import defaultdict

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a1_analyze as a1  # noqa: E402
import x2_census as xc  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
import costlog  # noqa: E402

RE_REC = re.compile(rb">(\d+)\nrevision>\n(?:restrictions>[^\n]*\n)?id>(\d+)\ntimestamp>0(\d)(\d+):(\d+)\ncontributor>\n"
                    rb"(?:username>([^\n]*)\nid>(\d+)|ip>([^\n]*))\n/contributor>\n")
CLASSES = ["revid", "ts", "uid"]
CI = {c: i + 5 for i, c in enumerate(CLASSES)}  # x2_sim class slots 5..7
LO, HI = 14_000_000, 18_100_000
ENC = {xc_c: None for xc_c in range(256)}


def enc_table():
    t = {}
    for s in range(256):
        t[a1.SWAP[s]] = s
    return t


def T_of(y, d, s):
    return (y * 372 + d) * 86400 + s


def ts_str(T):
    days, s = divmod(T, 86400)
    y, d = divmod(days, 372)
    return b"0%d%d:%d" % (y, d, s)


def walk(full_pre, want, tail_bytes=60_000_000):
    """One causal pass over the full block; returns {revid: predictions} for the revids in `want`."""
    with open(full_pre, "rb") as f:
        f.seek(0, 2)
        n = f.tell()
        f.seek(max(0, n - tail_bytes))
        t = f.read()
    e = t.rfind(b"  </page>\n") + 10
    keys, vals = [], []              # monotone zone: sorted revids and times
    dh = [0.0] + [1.0 / (d ** 1.5) for d in range(1, 65)]  # chain delta histogram (prior)
    dh[0] = 0.0
    n_chain, n_zone = 1.0, 2.0       # chain-continuation rate inside the renumbered zone
    n_lo, n_all = 1.0, 2.0           # rate of the renumbered zone
    last_chain = None
    scale = defaultdict(lambda: [8.0, 1.0])  # gap bucket -> running mean |err| (sum, n)
    uids = {}
    out = {}
    for k, m in enumerate(RE_REC.finditer(t, e)):
        r = int(m.group(2))
        T = T_of(int(m.group(3)), int(m.group(4)), int(m.group(5)))
        user, uid = m.group(6), m.group(7)
        pred = {}
        # --- revid: chain continuation
        if last_chain is not None:
            tot = sum(dh)
            pz = n_lo / n_all
            pc = n_chain / n_zone
            pred["revid"] = ([(b"%d\n" % (last_chain + d), dh[d] / tot) for d in range(1, 65)], pz * pc)
        # --- timestamp from the calibration
        i = bisect.bisect_left(keys, r)
        if r >= HI and keys:
            if 0 < i < len(keys):
                r0, r1, t0, t1 = keys[i - 1], keys[i], vals[i - 1], vals[i]
                tp = t0 + (t1 - t0) * (r - r0) / (r1 - r0)
                gap = r1 - r0
            else:
                j = min(max(i - 1, 0), len(keys) - 1)
                tp = vals[j] + 0.6 * (r - keys[j])  # about 0.6 s per revision in 2006
                gap = 10 ** 7
            gb = min(int(math.log2(gap + 1)), 30)
            sc = scale[gb]
            b = max(1.0, sc[0] / sc[1])
            pred["ts"] = (int(round(tp)), b, gb)
        # --- uid from the name
        if user is not None and user in uids:
            pred["uid"] = uids[user]
        if r in want:
            pred["k"] = k
            out[r] = pred
        # --- update the state with the true record
        if r >= HI:
            if "ts" in pred:
                tp, b, gb = pred["ts"]
                sc = scale[gb]
                sc[0] += min(abs(T - tp), 30000)
                sc[1] += 1
            bisect.insort(keys, r)
            vals.insert(bisect.bisect_left(keys, r), T)
        n_all += 1
        if LO <= r < HI:
            n_lo += 1
            n_zone += 1
            if last_chain is not None and 0 < r - last_chain <= 64:
                dh[r - last_chain] += 1
                n_chain += 1
            last_chain = r
        if user is not None:
            uids[user] = uid
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True)
    ap.add_argument("--cost", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--full", default="build/exp/A1/full/full.pre")
    ap.add_argument("--fields", default="revid,ts,uid")
    ap.add_argument("--maxw", type=int, default=2000)
    ap.add_argument("--wmul", type=float, default=8.0)
    args = ap.parse_args()
    fields = set(args.fields.split(","))
    enc = enc_table()

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
    recs = list(RE_REC.finditer(text, e))
    want = {int(m.group(2)) for m in recs}
    P = walk(args.full, want)
    print(f"slice block: {len(recs):,} records, {sum(1 for m in recs if int(m.group(2)) in P):,} found in the full block; "
          f"full-block index range {min(p['k'] for p in P.values()):,}..{max(p['k'] for p in P.values()):,}")
    slots = []
    stat = defaultdict(lambda: [0, 0])
    for m in recs:
        rv = int(m.group(2))
        pr = P.get(rv)
        if pr is None:
            continue
        if "revid" in fields and "revid" in pr:
            cands, w = pr["revid"]
            c = cpos(m.start(2))
            if c >= 0:
                act = stream[c:c + len(m.group(2)) + 1]
                stat["revid"][0] += 1
                stat["revid"][1] += any(encode(s) == act for s, _ in cands)
                slots.append((c, CI["revid"], act, [(encode(s), wt) for s, wt in cands]))
        if "ts" in fields and "ts" in pr:
            tp, b, gb = pr["ts"]
            W = int(min(args.maxw, max(20, args.wmul * b)))
            cands = []
            for T in range(max(0, tp - W), tp + W + 1):
                cands.append((encode(ts_str(T) + b"\n"), math.exp(-abs(T - tp) / b)))
            s0 = m.start(3) - 1  # the leading "0"
            c = cpos(s0)
            if c >= 0:
                L = m.end(5) + 1 - s0
                act = stream[c:c + L]
                stat["ts"][0] += 1
                stat["ts"][1] += any(s == act for s, _ in cands)
                slots.append((c, CI["ts"], act, cands))
        if "uid" in fields and "uid" in pr and m.group(7) is not None:
            c = cpos(m.start(7))
            if c >= 0:
                act = stream[c:c + len(m.group(7)) + 1]
                cs = encode(pr["uid"] + b"\n")
                stat["uid"][0] += 1
                stat["uid"][1] += cs == act
                slots.append((c, CI["uid"], act, [(cs, 1.0)]))
    slots.sort(key=lambda s: s[0])
    print(f"{len(slots):,} slots; per field (slots, actual among candidates): {dict(stat)}")
    with open(args.out, "wb") as f:
        f.write(struct.pack("<q", len(slots)))
        for c, cls, actual, cands in slots:
            f.write(struct.pack("<qBBB", int(c), cls, 1, len(actual)) + actual)
            f.write(struct.pack("<i", 0))
            agg = {}
            for s, w in cands:
                agg[s] = agg.get(s, 0.0) + w
            items = list(agg.items())[:65535]
            f.write(struct.pack("<Bf", 0, 1.0) + struct.pack("<H", len(items)))
            for s, w in items:
                f.write(struct.pack("<B", len(s)) + s + struct.pack("<f", max(w, 1e-30)))


if __name__ == "__main__":
    main()
