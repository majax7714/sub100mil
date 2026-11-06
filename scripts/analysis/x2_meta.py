#!/usr/bin/env python3
"""X2: what the record pays on the per-page metadata block (phda9 moves revision id, timestamp and contributor of
every page into a block after the last page), field by field.

Usage: python3 -P -E scripts/analysis/x2_meta.py --pre SLICE.PRE --cost COSTLOG [--dict english.dic]
Block format per page: ">N\nrevision>\nid>REVID\ntimestamp>0Y D:SSSSS\ncontributor>\n(username>NAME\nid>UID|ip>IP)\n/contributor>\n"
The timestamp is "0" + (year - 2001) + (31 (month - 1) + day - 1), ":" + seconds of the day (UTC).
"""
import argparse, os, re, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x2_census as x  # noqa: E402

RE_REC = re.compile(rb">(-?\d+)\nrevision>\n(?:restrictions>[^\n]*\n)?id>(\d+)\ntimestamp>(\d+):(\d+)\ncontributor>\n(?:username>([^\n]*)\nid>(\d+)|ip>([^\n]*))\n/contributor>\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True); ap.add_argument("--cost", required=True); ap.add_argument("--dict")
    a = ap.parse_args()
    text = open(a.pre, "rb").read()
    cc = np.load(os.path.join("build/exp/X2", "tcost_" + os.path.basename(a.cost) + ".npy"))
    st = x.pages_of(text)
    e = st[-1]
    tail = text[e:len(cc) - 2]
    fl = {k: [0, 0.0] for k in ("count", "revid", "ts_day", "ts_colon", "ts_sec", "username", "userid", "ip", "rest")}
    tot_rec = 0.0
    for m in RE_REC.finditer(tail):
        def S(g, nl=True):
            s, en = e + m.start(g), e + m.end(g) + (1 if nl else 0)
            return cc[en] - cc[s]
        tot_rec += cc[e + m.end()] - cc[e + m.start()]
        fl["count"][0] += 1; fl["count"][1] += S(1)
        fl["revid"][0] += 1; fl["revid"][1] += S(2)
        fl["ts_day"][0] += 1; fl["ts_day"][1] += S(3, False)
        fl["ts_colon"][1] += cc[e + m.end(3) + 1] - cc[e + m.end(3)]
        fl["ts_sec"][0] += 1; fl["ts_sec"][1] += S(4)
        if m.group(5) is not None:
            fl["username"][0] += 1; fl["username"][1] += S(5)
            fl["userid"][0] += 1; fl["userid"][1] += S(6)
        else:
            fl["ip"][0] += 1; fl["ip"][1] += S(7)
    n = fl["revid"][0]
    allf = sum(v[1] for v in fl.values())
    print(f"pages {n:,}; metadata records {tot_rec / 8:,.0f} B of record {cc[-1] / 8:,.0f} B ({100 * tot_rec / cc[-1]:.2f}%)")
    for k, (c, b) in fl.items():
        print(f"  {k:9s} n={c:7d} {b / 8:9,.0f} B  {b / max(c, 1):6.2f} bits/field")
    print(f"  tags etc: {(tot_rec - allf) / 8:,.0f} B")


main()
