#!/usr/bin/env python3
"""T1: causal slots for the interlanguage-title law (link title = the article's English title), for t1_sim.cpp.

Usage: python3 -P -E scripts/analysis/t1_slots.py --pre X.pre --cost COSTLOG --dict english.dic --out SLOTS.bin
            [--noparen W] [--hist]

phda9 moves an article's trailing interlanguage links (and its "</text>") into one group of the block after the
metadata block; the article keeps no "</text>". The k-th article without "</text>" therefore owns the k-th group
(groups end with "</text>"), and all titles are decoded before the block: decoder-valid in any page order.

A slot opens at every link title (after "[xx:"). Candidate: the coded bytes of the English title as the <title>
line coded it, plus "]" (and, with --noparen W > 0, the title without its trailing " (...)" at weight W).
Working in coded space means the real model copies coded bytes; slots whose coded form differs are counted.
The slot spans the coded bytes up to and including the first byte that leaves every candidate (or the whole
candidate when it matches), so the sim follows prefixes and pays for the divergence byte.

Class (decoder-valid, from earlier links only): 3 group-history states (first link of the group / an earlier link
of the group equalled T / none did) x 4 buckets of the language's running equality rate (links so far whose title
was T, Laplace: <0.05, <0.2, <0.5, >=0.5).
"""
import argparse, os, re, struct, sys
from collections import defaultdict
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import a1_analyze as a1  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
import costlog  # noqa: E402

RE_LINK = re.compile(rb"\[([a-z][a-z\-]{1,11}):([^\]\n]*)\](?=\n|</text>)")
CLASSES = [f"{h}/{l}" for h in ("first", "grpeq", "grpne") for l in ("r<.05", "r<.2", "r<.5", "r>=.5")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pre", required=True); ap.add_argument("--cost", required=True)
    ap.add_argument("--dict", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--noparen", type=float, default=0.0)
    ap.add_argument("--grp", type=float, default=0.0, help="weight of each earlier link title of the group (coded)")
    a = ap.parse_args()
    r = costlog.load(a.cost, mmap=False)
    stream = r["byte"].tobytes()
    dec, cmap, container = a1.wrt_decode(stream, a1.load_dict(a.dict))
    text = open(a.pre, "rb").read()
    if text[:len(dec)] != dec:
        raise SystemExit("WRT decode mismatch")
    first = np.full(len(dec) + 1, -1, np.int64)
    idx = np.arange(len(cmap))
    ok = ~container
    first[cmap[ok][::-1]] = idx[ok][::-1]
    # the escape byte 0x0C maps both coded bytes to the same text position; first[] then points at the escape

    def cpos(p):
        if p >= len(dec):
            return len(stream)
        c = first[p]
        return c if c >= 0 and cmap[c] == p else -1

    d = dec
    last = d.rfind(b"</page>\n")
    starts = [m.start() for m in re.finditer(rb"  <page>\n", d[:last])]
    titles = []  # (title text, coded title)
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else last
        if d.find(b"</text>", s, e) >= 0 or d.find(b'<text xml:space="preserve" />', s, e) >= 0:
            continue
        m = re.compile(rb"<title>(.*?)</title>").search(d, s, e)
        a0, b0 = cpos(m.start(1)), cpos(m.end(1))
        np_ = re.sub(rb" \([^)]*\)$", b"", m.group(1))
        c_np = None
        if np_ != m.group(1):
            e1 = cpos(m.start(1) + len(np_))
            if e1 >= 0:
                c_np = stream[a0:e1]
        titles.append((m.group(1), stream[a0:b0] if a0 >= 0 and b0 >= 0 else None, c_np))
    gs0 = d.find(b"/contributor>\n[", last)
    if gs0 < 0:
        raise SystemExit("no interlanguage block in the coded part")
    pos, gend = gs0 + 14, d.rfind(b"</text>")
    groups = []
    while pos < gend:
        q = d.find(b"</text>", pos)
        groups.append((pos, q))
        pos = q + 8
    if len(groups) == len(titles) + 1:
        groups = groups[1:]
    if len(groups) != len(titles):
        raise SystemExit(f"mapping broken: {len(titles)} titles, {len(groups)} groups")
    rate = defaultdict(lambda: [0, 0])
    slots, st = [], defaultdict(int)
    for (t, ct, cnp), (s, e) in zip(titles, groups):
        grp = 0  # 0 first link, 1 an earlier link equalled T, 2 none did
        earlier = []
        for m in RE_LINK.finditer(d, s, e + 7):
            lang, x = m.group(1), m.group(2)
            n_eq, n = rate[lang]
            rr = (n_eq + 0.05) / (n + 1)
            lb = 0 if rr < 0.05 else 1 if rr < 0.2 else 2 if rr < 0.5 else 3
            cls = grp * 4 + lb
            eq = x == t
            rate[lang][0] += eq; rate[lang][1] += 1
            c = cpos(m.start(2))
            st["links"] += 1
            if c < 0 or ct is None:
                st["no coded boundary"] += 1
            else:
                cands = [(ct + b"]", 1.0)]
                if cnp is not None and a.noparen > 0:
                    cands.append((cnp + b"]", a.noparen))
                if a.grp > 0:
                    for ce in earlier:
                        if all(ce != cs for cs, _ in cands):
                            cands.append((ce, a.grp))
                best = 0
                for cs, _ in cands:
                    k = 0
                    while k < len(cs) and c + k < len(stream) and stream[c + k] == cs[k]:
                        k += 1
                    best = max(best, k if k == len(cs) else k + 1)
                    if k == len(cs):
                        st["coded match"] += 1
                if eq and not any(stream[c:c + len(cs)] == cs for cs, _ in cands):
                    st["eqT but coded differs"] += 1
                slots.append((c, cls, stream[c:c + best], cands))
            grp = 1 if (grp == 1 or eq) else 2
            c1 = cpos(m.end(2) + 1)
            if c >= 0 and c1 >= 0:
                earlier.append(stream[c:c1])
    print(dict(st), file=sys.stderr)
    with open(a.out, "wb") as f:
        f.write(struct.pack("<q", len(slots)))
        for c, cls, actual, cands in slots:
            actual = actual[:255]
            f.write(struct.pack("<qBBB", int(c), cls, 1, len(actual)) + actual)
            f.write(struct.pack("<i", 0))
            f.write(struct.pack("<Bf", 0, 1.0) + struct.pack("<H", len(cands)))
            for cs, w in cands:
                cs = cs[:255]
                f.write(struct.pack("<B", len(cs)) + cs + struct.pack("<f", w))
    print(f"{len(slots):,} slots", file=sys.stderr)


if __name__ == "__main__":
    main()
