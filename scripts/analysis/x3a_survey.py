#!/usr/bin/env python3
"""X3a survey counts on the C3 full-run stream (build/exp/X3a/full.txt + full.cost, pages.tsv, meta_records.tsv):
redirect pages (title / target bits, targets that are earlier titles, titles seen earlier as link targets),
comments (bot / section / move classes), interlanguage block (language code vs title bits, 'robot Adding: xx'
comments), year-article navigation tables.  python3 -P -E scripts/analysis/x3a_survey.py
"""
import mmap, re, csv, collections, sys
import numpy as np
D = 'build/exp/X3a'
f = open(D + '/full.txt', 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
cost = np.memmap(D + '/full.cost', dtype='<f4', mode='r')
def S(a, b): return float(np.asarray(cost[a:b], dtype=np.float64).sum())
P = list(csv.DictReader(open(D + '/pages.tsv'), delimiter='\t', quoting=csv.QUOTE_NONE))
n = len(P); KB = lambda bits: f'{bits / 8e3:8.1f} KB'
# ---- redirects
titles = set(); red = [p for p in P if p['redirect']]
tb = sum(float(p['title_bits']) for p in red); xb = sum(float(p['text_bits']) for p in red)
seen_t = 0; seen_bits = 0; alltitles = set(p['title'] for p in P if not p['redirect'])
norm = lambda s: s.replace('_', ' ').strip()
for p in red:
    t = norm(p['redirect']); t = t[:1].upper() + t[1:]
    if t.split('#')[0] in alltitles: seen_t += 1; seen_bits += float(p['text_bits'])
print(f'redirect pages {len(red)}: titles {KB(tb)} ({tb/len(red):.1f} bits each); text (#REDIRECT [target]...) {KB(xb)} ({xb/len(red):.1f} each)')
print(f'  target is an article title already coded: {seen_t} ({seen_t/len(red):.0%}), their text bits {KB(seen_bits)}')
# redirect titles seen earlier as a link target in article text (article part = before the first tail redirect)
first_red = int(next(p['start'] for p in P[172277:]))
links = set()
for mm in re.finditer(rb'\[([^\[\]\|\n:#]{1,80})[\]\|#]', m[:first_red]):
    links.add(mm.group(1).strip())
hit = 0; hb = 0
for p in P[172277:]:
    t = p['title'].encode()
    if t in links or (t[:1].lower() + t[1:]) in links: hit += 1; hb += float(p['title_bits'])
tail = P[172277:]; ttb = sum(float(p['title_bits']) for p in tail)
print(f'  tail (id-order) pages {len(tail)}: title bits {KB(ttb)}; title seen earlier as a link target: {hit} ({hit/len(tail):.0%}), paying {KB(hb)} ({hb/max(1,hit):.1f} bits each); distinct link targets {len(links)}')
del links
# ---- comments
cls = collections.Counter(); clb = collections.Counter()
for p in P:
    if not int(p['hascomment']): continue
    c = p['comment']; b = float(p['comment_bits'])
    k = ('robot interwiki' if re.match(r'(robot|warnfile|Robot)\s', c) and re.search(r'Adding|Modifying|Removing', c) else
         'other bot/robot' if re.search(r'(?i)\bbot\b|robot|AWB|popups', c) else
         'section /* */' if c.startswith('/*') else 'revert' if re.match(r'(?i)(rv|revert)', c) else
         'moved/redirect' if re.search(r'(?i)moved? |redirect', c) else 'template-ish {..}' if c.startswith('{') else 'other')
    cls[k] += 1; clb[k] += b
for k, v in clb.most_common(): print(f'comment class {k:18s} n={cls[k]:6d} {KB(v)} ({v/cls[k]:.1f} bits each)')
mb = 0; nm = 0
for mm in re.finditer(rb'      <minor />\n', m[:895000000]): mb += S(mm.start(), mm.end()); nm += 1
print(f'<minor /> lines {nm}: {KB(mb)}; ', end='')
# cost of the first byte after </title>\n for pages without minor ~ the minor decision: approximate with the line head
# ---- interlanguage block
ms = m.find(b'\nrevision>\nid>', 894000000); ib = m.find(b'\n[', ms) + 1; ie = m.find(b'<mediawiki', ib)
blk = m[ib:ie]; c = np.asarray(cost[ib:ie], dtype=np.float64); cs = np.concatenate([[0], np.cumsum(c)])
lang = 0; tit = 0; term = 0; nl = 0; ng = 0
for mm in re.finditer(rb'\[([a-z\-]{2,12}):([^\]\n]*)\](</text>)?\n?', blk):
    nl += 1; lang += cs[mm.start(2)] - cs[mm.start()]; tit += cs[mm.end(2)] - cs[mm.start(2)]; term += cs[mm.end()] - cs[mm.end(2)]
    if mm.group(3): ng += 1
print(f'\ninterlanguage block {ie-ib} B, C3 cost {KB(cs[-1])}: links {nl}, groups {ng}; "[xx:" codes {KB(lang)} ({lang/nl:.2f} bits each); titles {KB(tit)} ({tit/nl:.1f}); "]", newline, </text> {KB(term)} ({term/nl:.2f})')
nrob = sum(1 for p in P if re.search(r'(Adding|Modifying):', p['comment']))
nlang = sum(len(re.findall(r'\b[a-z]{2,3}(?:-[a-z]+)?\b', p['comment'].split(':', 1)[1])) for p in P if re.search(r'^(robot|warnfile)\s+(Adding|Modifying):', p['comment']))
print(f'pages whose comment names interwiki languages (Adding/Modifying:): {nrob}; language codes named ~{nlang}')
# ---- year navigation tables
ny = 0; yb = 0; dig = 0
for mm in re.finditer(rb"<small>'''Years:'''</small><br>[^\n]*\n", m[:895000000]):
    ny += 1; yb += S(mm.start(), mm.end())
    for d in re.finditer(rb'\d+', mm.group(0)): dig += S(mm.start() + d.start(), mm.start() + d.end())
print(f"year nav rows ('Years:' line) {ny}: {KB(yb)} total, digits {KB(dig)}")
nd = 0; db = 0
for mm in re.finditer(rb"'''\[Decades\]:'''</small>[^\n]*\n[^\n]*\n[^\n]*'''\[Centuries\]:'''[^\n]*\n", m[:895000000]):
    nd += 1; db += S(mm.start(), mm.end())
print(f'decade + century rows {nd}: {KB(db)}')
