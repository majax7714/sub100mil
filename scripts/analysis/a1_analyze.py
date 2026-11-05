#!/usr/bin/env python3
"""A1 analysis: where the record's bits go (content classes) and retrieval headroom.

Usage:
  a1_analyze.py --cost COSTLOG --pre SLICE.PRE --dict english.dic [--match PREFIX] [--out DIR]

  COSTLOG   per-byte cost log of the record (patches/tools/per-byte-cost, FX2_COSTLOG=...)
  SLICE.PRE slicetool pre output of the same slice (used to verify the WRT decode)
  PREFIX    output prefix of scripts/analysis/a1_match run with --sam on COSTLOG (.pos/.mix)

Method (documented in docs/design/A1-retrieval.md):
  1. Decode the coded (WRT) stream back to slice.pre text, keeping for every coded byte the
     position of the first decoded character it produces (flags and word-code bytes map to the
     word they encode).
  2. Classify every decoded character with simple heuristics on the phda9 text, where
     '[x]' is an internal link ('[[x]]' in the original), '[[x]]' an external link, '{x}' a
     template, '{{| ... |}}' a table. Painting order (later wins):
       prose < html/entity < heading < link anchor < link target < image/file < external link
       < category < template < table < infobox < ref/citation < math;
     whole-page overrides: redirect pages; and the non-article sections of slice.pre
     (header, metadata block, interlanguage block, intro/coda XML).
  3. Each coded byte takes the class of its decoded character.
"""
import argparse
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../patches/tools/per-byte-cost"))
import costlog  # noqa: E402

CLASSES = [
    "prose", "html/entity", "heading", "link anchor", "link target", "image/file", "external link/URL",
    "category", "template", "table", "infobox", "ref/citation", "math",
    "redirect page", "page XML (tags)", "title", "edit comment", "metadata block (ids/time/user)",
    "interlanguage block", "header/intro/coda", "container (block hdr)",
]
C = {name: i for i, name in enumerate(CLASSES)}

# ------------------------------------------------------------------ WRT decode with mapping


def load_dict(path):
    data = open(path, "rb").read()
    words = re.findall(rb"[a-z]+", data)
    rev = {}
    b1, b2, b3 = 80, 80 + 3840, 80 + 3840 + 40960
    for n, w in enumerate(words):
        if n < b1:
            code = 0x80 + n
        elif n < b2:
            code = (0xD0 + (n - b1) // 80) + ((0x80 + (n - b1) % 80) << 8)
        elif n < b3:
            m = n - b2
            code = (0xF0 + (m // 80) // 32) + ((0xD0 + (m // 80) % 32) << 8) + ((0x80 + m % 80) << 16)
        else:
            break
        rev[code] = w
    return rev


SWAP = list(range(256))
for c in range(256):
    d = c
    if ord("{") <= d < 127:
        d += ord("P") - ord("{")
    elif ord("P") <= d < ord("T"):
        d -= ord("P") - ord("{")
    elif ord(":") <= d <= ord("?") or ord("J") <= d <= ord("O"):
        d ^= 0x70
    if d in (ord("X"), ord("`")):
        d ^= ord("X") ^ ord("`")
    SWAP[c] = d


def wrt_decode(stream, rev):
    """Returns (decoded bytes, map coded index -> decoded position, container mask)."""
    n = len(stream)
    cmap = np.zeros(n, np.int64)
    container = np.zeros(n, bool)
    out = bytearray()
    i = 0
    # The coded stream is preprocessor::Encode's output minus its 5-byte block header (type +
    # length, which the archive header carries): it starts with the TEXT block's WRT flag byte
    # (7 = WRT on). A stream from `cmix -s` (minus the 5-byte storage header) still has the block
    # header; skip it if present.
    if stream[0] != 7 and len(stream) > 6 and stream[5] == 7:
        container[:5] = True
        stream = stream[5:]
        cmap[:5] = 0
        cmap = cmap[5:]
    wrt = stream[0]
    container[0] = True
    i = 1
    n = len(stream)
    if wrt == 0:
        raise SystemExit("WRT disabled in this block; not supported")
    upper = capital = False
    st = stream
    while i < n:
        start = len(out)
        c = SWAP[st[i]]
        cmap[i] = start
        i += 1
        if c == 0x0C:  # escape
            upper = False
            cmap[i] = start
            out.append(SWAP[st[i]])
            i += 1
        elif c == 0x07:
            upper = True
        elif c == 0x40:
            capital = True
        elif c == 0x06:
            upper = False
        elif c >= 0x80:
            code = c
            if c > 0xCF:
                c2 = SWAP[st[i]]
                cmap[i] = start
                i += 1
                code += c2 << 8
                if c2 > 0xCF:
                    c3 = SWAP[st[i]]
                    cmap[i] = start
                    i += 1
                    code += c3 << 16
            w = bytearray(rev[code])
            if capital:
                w[0] -= 32
                capital = False
            if upper:
                w = w.upper()
            out += w
        else:
            if not (ord("a") <= c <= ord("z") or ord("A") <= c <= ord("Z")):
                upper = False
            if capital or upper:
                c = c - ord("a") + ord("A")
            capital = False
            out.append(c & 0xFF)
    return bytes(out), np.minimum(cmap, len(out) - 1), container


# ------------------------------------------------------------------ classification


def paint(cls, spans, value):
    for a, b in spans:
        cls[a:b] = value


def classify(text):
    n = len(text)
    cls = np.full(n, C["prose"], np.uint8)
    # sections of slice.pre: header line | articles | metadata | interlanguage | intro+coda+trailer
    hdr_end = text.find(b"\n") + 1
    art_end = text.rfind(b"  </page>\n") + len(b"  </page>\n")
    meta_re = re.compile(rb"(>\d+|/?[a-z]+>.*|\d+|)")
    pos = art_end
    while True:
        e = text.find(b"\n", pos)
        if e < 0 or not meta_re.fullmatch(text, pos, e):
            break
        pos = e + 1
    meta_end = pos
    intro = text.find(b"<mediawiki", meta_end)
    if intro < 0:
        intro = n
    sections = dict(header=(0, hdr_end), articles=(hdr_end, art_end), metadata=(art_end, meta_end),
                    interlanguage=(meta_end, intro), xml_tail=(intro, n))

    art = text[:art_end]
    # page XML: everything outside <text>..</text>, then titles/comments inside it
    cls[hdr_end:art_end] = C["page XML (tags)"]
    text_spans = []
    for m in re.finditer(rb'<text xml:space="preserve">(.*?)</text>', art, re.S):
        text_spans.append((m.start(1), m.end(1)))
    for a, b in text_spans:
        cls[a:b] = C["prose"]
    for m in re.finditer(rb"<title>(.*?)</title>", art):
        cls[m.start(1):m.end(1)] = C["title"]
    for m in re.finditer(rb"<comment>(.*?)</comment>", art, re.S):
        cls[m.start(1):m.end(1)] = C["edit comment"]

    # inline markup inside article text
    def spans(pattern, flags=0, group=0):
        return [(m.start(group), m.end(group)) for m in re.finditer(pattern, art, flags)]

    in_text = np.zeros(n, bool)
    for a, b in text_spans:
        in_text[a:b] = True

    def paint_in_text(sp, value):
        for a, b in sp:
            sel = in_text[a:b]
            cls[a:b][sel] = value

    paint_in_text(spans(rb"<[^<>\n]{1,200}>|&[a-z]{2,8};|&#\d+;|&\x05"), C["html/entity"])
    paint_in_text(spans(rb"(?m)^=+[^\n]*=+[ \t]*$"), C["heading"])
    # internal links with a stack over single brackets (phda9: '[' = original '[[')
    link_t, link_a, image, cat = [], [], [], []
    stack = []
    for m in re.finditer(rb"\[\[|\]\]|\[|\]|\n\n", art):
        tok = m.group()
        if tok == b"\n\n" or tok in (b"[[", b"]]"):
            if tok == b"\n\n":
                stack.clear()
            continue
        if tok == b"[":
            stack.append(m.start())
        elif stack:
            a = stack.pop()
            b = m.end()
            body = art[a + 1:b - 1]
            if body.startswith((b"Category:", b"category:")):
                cat.append((a, b))
            elif body.startswith((b"Image:", b"image:", b"File:", b"Media:")):
                image.append((a, b))
            else:
                bar = body.find(b"|")
                if bar < 0:
                    link_t.append((a, b))
                else:
                    link_t.append((a, a + 1 + bar + 1))
                    link_a.append((a + 2 + bar, b))
    paint_in_text(link_a, C["link anchor"])
    paint_in_text(link_t, C["link target"])
    paint_in_text(image, C["image/file"])
    paint_in_text(spans(rb"\[\[[^\[\]\n]*\]\]|https?://[^\s\]\|<]+"), C["external link/URL"])
    paint_in_text(cat, C["category"])
    # templates / tables / infoboxes with a stack (phda9: '{' = original '{{', '{{|' = original '{|')
    tmpl, table, infobox = [], [], []
    stack = []
    text_end_iter = iter(sorted(b for a, b in text_spans))
    next_text_end = next(text_end_iter, n)
    for m in re.finditer(rb"\{\{\||\|\}\}|\{\{|\}\}|\{|\}", art):
        p = m.start()
        while p >= next_text_end:  # templates never cross </text>
            stack.clear()
            next_text_end = next(text_end_iter, n)
        tok = m.group()
        if tok == b"{{|":
            stack.append(("table", p))
        elif tok == b"|}}":
            while stack:
                kind, a = stack.pop()
                if kind == "table":
                    table.append((a, m.end()))
                    break
        elif tok == b"{":
            stack.append(("tmpl", p))
        elif tok == b"}":
            if stack and stack[-1][0] == "tmpl":
                kind, a = stack.pop()
                name = art[a + 1:a + 40].lstrip().lower()
                if name.startswith(b"infobox") or re.match(rb"[a-z_ ]*box\b", name):
                    infobox.append((a, m.end()))
                else:
                    tmpl.append((a, m.end()))
        # '{{' / '}}' (original single braces) are literal
    paint_in_text(tmpl, C["template"])
    paint_in_text(table, C["table"])
    paint_in_text(infobox, C["infobox"])
    paint_in_text(spans(rb"<ref[^>]*/>|<ref[^>]*>.*?</ref>", re.S), C["ref/citation"])
    paint_in_text(spans(rb"<math>.*?</math>", re.S), C["math"])
    # redirect pages
    for a, b in text_spans:
        if art[a:a + 9].upper() == b"#REDIRECT":
            cls[a:b] = C["redirect page"]

    a, b = sections["header"]; cls[a:b] = C["header/intro/coda"]
    a, b = sections["metadata"]; cls[a:b] = C["metadata block (ids/time/user)"]
    a, b = sections["interlanguage"]; cls[a:b] = C["interlanguage block"]
    a, b = sections["xml_tail"]; cls[a:b] = C["header/intro/coda"]
    return cls, sections


def lexical(text):
    t = np.frombuffer(text, np.uint8)
    lex = np.zeros(len(t), np.uint8)  # 0 letters (ASCII), 1 digit, 2 non-ASCII, 3 space/punct/other
    letters = ((t >= 97) & (t <= 122)) | ((t >= 65) & (t <= 90))
    lex[~letters] = 3
    lex[(t >= 48) & (t <= 57)] = 1
    lex[t >= 0x80] = 2
    return lex


LEXNAMES = ["ASCII letters", "digits", "non-ASCII (UTF-8)", "space/punct/other"]

# ------------------------------------------------------------------ reporting


def table(rows, header):
    w = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
    out = ["| " + " | ".join(str(h) for h in header) + " |", "|" + "|".join("---:" if i else "---" for i in range(len(header))) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    del w
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cost", required=True)
    ap.add_argument("--pre", required=True)
    ap.add_argument("--dict", required=True)
    ap.add_argument("--match")
    ap.add_argument("--out")
    args = ap.parse_args()

    r = costlog.load(args.cost, mmap=False)
    stream = r["byte"].tobytes()
    rev = load_dict(args.dict)
    text, cmap, container = wrt_decode(stream, rev)
    pre = open(args.pre, "rb").read()
    if text != pre:
        k = next((i for i in range(min(len(text), len(pre))) if text[i] != pre[i]), min(len(text), len(pre)))
        raise SystemExit(f"WRT decode mismatch at {k} (decoded {len(text)} vs pre {len(pre)})")
    print(f"WRT decode verified: {len(stream):,} coded bytes -> {len(text):,} slice.pre bytes\n")

    tcls, sections = classify(text)
    tlex = lexical(text)
    bcls = tcls[cmap]
    bcls[container] = C["container (block hdr)"]
    blex = tlex[cmap]
    cost = r["cost"].astype(np.float64)
    ctf = r["cost_tf"].astype(np.float64)
    cfx = r["cost_fx"].astype(np.float64)
    tot = cost.sum()
    n = len(cost)
    # decoded characters per class (text size, for bits per *original* char)
    tcount = np.bincount(tcls, minlength=len(CLASSES))

    out = []
    out.append(f"Slice: {len(text):,} slice.pre bytes, {n:,} coded bytes, record total {tot / 8:,.0f} B "
               f"({tot / n:.4f} bits/coded byte)\n")
    out.append("Sections of slice.pre: " + ", ".join(f"{k} {b - a:,} B" for k, (a, b) in sections.items()) + "\n")
    rows = []
    order = np.argsort(-np.bincount(bcls, weights=cost, minlength=len(CLASSES)))
    for ci in order:
        sel = bcls == ci
        nb = int(sel.sum())
        if nb == 0:
            continue
        bits = cost[sel].sum()
        rows.append([CLASSES[ci], f"{tcount[ci]:,}", f"{nb:,}", f"{bits / 8:,.0f}", f"{100 * bits / tot:.1f}%",
                     f"{bits / max(tcount[ci], 1):.3f}", f"{bits / nb:.3f}",
                     f"{ctf[sel].sum() / bits:.2f}", f"{cfx[sel].sum() / bits:.2f}"])
    out.append("### Bits by content class (record, 10M slice)\n")
    out.append(table(rows, ["class", "text bytes", "coded bytes", "record bytes", "share", "bits/text byte",
                            "bits/coded byte", "TF-alone / record", "fxcm-alone / record"]))
    out.append("\n### Lexical overlay (all classes)\n")
    rows = []
    for li in range(4):
        sel = blex == li
        bits = cost[sel].sum()
        rows.append([LEXNAMES[li], f"{int((tlex == li).sum()):,}", f"{int(sel.sum()):,}", f"{bits / 8:,.0f}",
                     f"{100 * bits / tot:.1f}%", f"{bits / max(int((tlex == li).sum()), 1):.3f}"])
    out.append(table(rows, ["lexical type", "text bytes", "coded bytes", "record bytes", "share", "bits/text byte"]))

    # how concentrated are the bits? (record cost per coded byte)
    out.append("\n### Concentration: record bits by the record's own per-byte cost\n")
    rows = []
    edges = [0, 0.1, 0.5, 1, 2, 4, 8, 1e9]
    for lo, hi in zip(edges[:-1], edges[1:]):
        sel = (cost >= lo) & (cost < hi)
        rows.append([f"{lo:g}-{hi:g}" if hi < 1e9 else f">={lo:g}", f"{100 * sel.mean():.1f}%", f"{cost[sel].sum() / 8:,.0f}",
                     f"{100 * cost[sel].sum() / tot:.1f}%", f"{ctf[sel].sum() / max(cost[sel].sum(), 1):.3f}"])
    out.append(table(rows, ["record bits on byte", "share of coded bytes", "record bytes", "share of bits", "TF-alone / record"]))
    # word codes vs other bytes in the coded stream
    b = r["byte"]
    kinds = [("WRT word-code bytes (>=0x80, incl. escaped UTF-8)", b >= 0x80), ("letters (non-dictionary)", (b >= 97) & (b <= 122)),
             ("digits", (b >= 48) & (b <= 57)), ("case flags 0x40/0x07/0x06", (b == 0x40) | (b == 7) | (b == 6)),
             ("space", b == 32), ("newline", b == 10)]
    rest = np.ones(n, bool)
    rows = []
    for name, sel in kinds:
        sel = sel & rest
        rest &= ~sel
        rows.append([name, f"{int(sel.sum()):,}", f"{cost[sel].sum() / 8:,.0f}", f"{100 * cost[sel].sum() / tot:.1f}%",
                     f"{cost[sel].mean():.3f}"])
    rows.append(["other punctuation/markup", f"{int(rest.sum()):,}", f"{cost[rest].sum() / 8:,.0f}",
                 f"{100 * cost[rest].sum() / tot:.1f}%", f"{cost[rest].mean():.3f}"])
    out.append("\n### Coded-stream byte kinds (multi-byte word codes count each byte)\n")
    out.append(table(rows, ["kind", "coded bytes", "record bytes", "share", "bits/coded byte"]))

    if args.match:
        out.append(headroom(args.match, r, bcls, cost, ctf, cfx))
    rep = "\n".join(out)
    print(rep)
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        open(os.path.join(args.out, "a1_report.md"), "w").write(rep + "\n")
        np.save(os.path.join(args.out, "class_per_coded_byte.npy"), bcls)


POSDT = np.dtype([("L", "<u2"), ("M1", "<u2"), ("nconts", "u1"), ("pad", "u1"), ("flags", "<u2"),
                  ("lp_len", "<u2"), ("lp_pred", "u1"), ("k32_pred", "u1"), ("lp_dist", "<u4"),
                  ("k16_dist", "<u4"), ("s16_dist", "<u4")])
PNAMES = ["k8", "k16", "k32", "k64", "lp", "skip16", "skip32"]


def headroom(prefix, r, bcls, cost, ctf, cfx):
    pos = np.fromfile(prefix + ".pos", POSDT)
    n = len(cost)
    assert len(pos) == n
    tot = cost.sum()
    out = ["\n## Retrieval headroom\n"]
    fl = pos["flags"].astype(np.uint32)
    buckets = [(0, 0.1), (0.1, 1), (1, 4), (4, 1e9)]
    bnames = ["<0.1", "0.1-1", "1-4", ">=4"]

    def row(name, sel_valid, sel_ok):
        nv, nk = int(sel_valid.sum()), int(sel_ok.sum())
        bits_ok = cost[sel_ok].sum()
        cells = [name, f"{100 * nv / n:.1f}%", f"{100 * nk / max(nv, 1):.1f}%", f"{100 * nk / n:.1f}%",
                 f"{bits_ok / 8:,.0f}", f"{100 * bits_ok / tot:.2f}%"]
        for lo, hi in buckets:
            s = sel_ok & (cost >= lo) & (cost < hi)
            cells.append(f"{cost[s].sum() / 8:,.0f}")
        return cells

    rows = []
    L = pos["L"].astype(np.int64)
    M = pos["M1"].astype(np.int64) - 1
    for k in (8, 16, 32, 64):
        rows.append(row(f"oracle: some earlier k={k} context continued correctly (SAM M>={k})", L >= k, M >= k))
    det = (pos["nconts"] == 1)
    for k in (16, 32):
        rows.append(row(f"oracle-deterministic: L>={k}, single continuation seen, correct", (L >= k) & det, (L >= k) & det & (M >= L)))
    for p, name in enumerate(PNAMES):
        v = (fl >> (2 * p)) & 1 == 1
        ok = (fl >> (2 * p + 1)) & 1 == 1
        rows.append(row(f"most-recent {name}", v, ok))
    lp_v = (fl >> 8) & 1 == 1
    lp_ok = (fl >> 9) & 1 == 1
    lpl = pos["lp_len"].astype(np.int64)
    for k in (16, 32, 64, 128):
        rows.append(row(f"most-recent lp, verified len>={k}", lp_v & (lpl >= k), lp_ok & (lpl >= k)))
    # template-only: skip-gram right where every exact predictor is wrong/absent
    exact_ok = np.zeros(n, bool)
    for p in range(5):
        exact_ok |= (fl >> (2 * p + 1)) & 1 == 1
    s16_ok = (fl >> 11) & 1 == 1
    s32_ok = (fl >> 13) & 1 == 1
    rows.append(row("skip16|skip32 right, all exact predictors wrong/absent", ((fl >> 10) & 1 == 1) | ((fl >> 12) & 1 == 1),
                    (s16_ok | s32_ok) & ~exact_ok))
    out.append("Coverage = share of coded bytes with a prediction; acc = share of those that are right;\n"
               "'record bytes on right' = what the record paid on the bytes the predictor gets right,\n"
               "split by the record's own cost of that byte in bits.\n")
    out.append(table(rows, ["predictor", "coverage", "acc", "right/all", "record bytes on right", "of total",
                            "rec<0.1b", "0.1-1b", "1-4b", ">=4b"]))

    # does the record's own match machinery already see it?
    cm_ok = (r["cm_len"] >= 16) & (r["cm_pred"] == r["byte"])
    fx_ok = (r["fx_len"] > 0) & (r["fx_pred"] == r["byte"])
    rec_sees = cm_ok | fx_ok
    out.append("\n### Long-match-right bytes (lp len>=32 right): does the record's match machinery agree?\n")
    sel = lp_ok & (lpl >= 32)
    rows = []
    for name, s in (("record match model also right (fxcm MM2 or cmix Match>=16 bits)", sel & rec_sees),
                    ("record match models silent or wrong", sel & ~rec_sees)):
        nb = int(s.sum())
        rows.append([name, f"{nb:,}", f"{cost[s].sum() / 8:,.0f}", f"{cost[s].sum() / max(nb, 1):.3f}",
                     f"{ctf[s].sum() / max(nb, 1):.3f}", f"{cfx[s].sum() / max(nb, 1):.3f}"])
    out.append(table(rows, ["subset", "bytes", "record bytes", "record bits/byte", "TF-alone bits/byte", "fxcm-alone bits/byte"]))

    # does the transformer alone already know far repeats? (it is trained on enwik9; its context is one article)
    out.append("\n### Long-match-right bytes (lp len>=32 right) by distance to the source\n")
    d = pos["lp_dist"].astype(np.int64)
    rows = []
    for lo, hi, name in ((0, 1024, "<1K (inside the transformer's SWA window)"), (1024, 10_000, "1K-10K"),
                         (10_000, 100_000, "10K-100K"), (100_000, 1_000_000, "100K-1M"), (1_000_000, 1 << 40, ">=1M")):
        s = sel & (d >= lo) & (d < hi)
        nb = int(s.sum())
        rows.append([name, f"{nb:,}", f"{cost[s].sum() / 8:,.0f}", f"{cost[s].sum() / max(nb, 1):.3f}",
                     f"{ctf[s].sum() / max(nb, 1):.3f}", f"{cfx[s].sum() / max(nb, 1):.3f}"])
    out.append(table(rows, ["distance", "bytes", "record bytes", "record bits/byte", "TF-alone bits/byte", "fxcm-alone bits/byte"]))

    # opportunity bucket by class
    out.append("\n### Opportunity bucket by class: lp (len>=16) right AND record paid >= 1 bit\n")
    opp = lp_ok & (lpl >= 16) & (cost >= 1)
    rows = []
    for ci in np.argsort(-np.bincount(bcls[opp], weights=cost[opp], minlength=len(CLASSES))):
        s = opp & (bcls == ci)
        if not s.any():
            continue
        rows.append([CLASSES[ci], f"{int(s.sum()):,}", f"{cost[s].sum() / 8:,.0f}", f"{cost[s].mean():.2f}"])
    out.append(table(rows[:12], ["class", "bytes", "record bytes there", "mean record bits"]))

    # first half vs second half
    out.append("\n### History growth: first vs second half of the slice\n")
    rows = []
    h = n // 2
    for name, s in (("first half", slice(0, h)), ("second half", slice(h, n))):
        c = cost[s]
        o = (lp_ok & (lpl >= 32))[s]
        o2 = ((lp_ok & (lpl >= 16)) & (cost >= 1))[s]
        mm = (M >= 32)[s]
        rows.append([name, f"{c.sum() / 8:,.0f}", f"{c.mean():.4f}", f"{100 * mm.mean():.1f}%", f"{100 * o.mean():.1f}%",
                     f"{c[o].sum() / 8:,.0f}", f"{c[o2].sum() / 8:,.0f}"])
    out.append(table(rows, ["part", "record bytes", "bits/coded byte", "oracle M>=32", "lp32 right", "record bytes on lp32-right",
                            "opportunity bytes (lp16 right, rec>=1b)"]))

    if os.path.exists(prefix + ".mix"):
        mix = np.fromfile(prefix + ".mix", np.float32).reshape(-1, 2).astype(np.float64)
        base, withr = mix[:, 0], mix[:, 1]
        out.append("\n### Simulated final-stage retrieval mixer (realistic gain estimate)\n")
        out.append(f"record {tot / 8:,.0f} B; recalibration-only mixer {base.sum() / 8:,.0f} B; with retrieval inputs "
                   f"{withr.sum() / 8:,.0f} B; **retrieval gain {(base.sum() - withr.sum()) / 8:,.0f} B "
                   f"({100 * (base.sum() - withr.sum()) / base.sum():.3f}%)**\n")
        rows = []
        g = base - withr
        for ci in np.argsort(-np.bincount(bcls, weights=g, minlength=len(CLASSES))):
            s = bcls == ci
            if not s.any():
                continue
            rows.append([CLASSES[ci], f"{g[s].sum() / 8:,.0f}", f"{100 * g[s].sum() / max(cost[s].sum(), 1):.2f}%"])
        out.append(table(rows, ["class", "gain bytes", "gain % of class"]))
    return "\n".join(out)


if __name__ == "__main__":
    main()
