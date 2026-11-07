# T1: intra-article field-copy / derivation laws, and the interlanguage title copy

**Verdict: GO, for one law.** Of the field-copy laws surveyed (sort keys, birth/death categories, bold restatement,
infobox names, redirect targets, census categories, interlanguage titles), every in-article one is already cheap for
the record: the title sits in the transformer's window and the CM's match models copy it, so the record pays 0.5–6
bits per instance and the in-article laws together hold about **37 KB of record bits at stake** (an UPPER BOUND;
§2). The one that pays is a field copy **across phda9's block split**: phda9 moves every article's trailing
interlanguage links into a block after the metadata block, where neither the transformer (fresh 1,024-token context)
nor the CM knows which article a group belongs to. But the mapping is decoder-valid (**the k-th page without
`</text>` owns the k-th group**), and in 48,820 links (52% of the 93,700 groups) the title is the English title
itself. A causal final stage that offers the title's coded bytes as a candidate gains **−580 B on enwik9_10M
(1,261,700 → 1,261,120, −0.046%)** and **−1,059 B (−3.13%) on a reordered interwiki proxy**, both round-trip
verified and matching the causal sim within 6 B. **Full enwik9 ESTIMATE about 72 KB, range 48–90 KB**, for under
0.01 h of CPU, 19 MB of RAM and about 16 KB of unpacked binary.

Classification: **model addition (non-transformer specialist, final stage)**. Labels: **[M]** measured here,
**[ESTIMATE]** derived by the argument next to it, **UPPER BOUND** a ceiling, not an expected gain.

## 0. Method and reproduction

| Step | Script |
|---|---|
| In-article laws: instances, law holds, record bits on the derivable bytes | `scripts/analysis/t1_survey.py` |
| Census categories (lead link / "type in STATE") | `build/exp/T1/census_cat.py` (one-off) |
| Interlanguage titles vs the English title, by class, with the decoder-valid group mapping | `scripts/analysis/t1_iw.py` |
| Offline slots and per-bit sim (E12/X2 method) | `scripts/analysis/t1_slots.py`, `scripts/analysis/t1_sim.cpp` |
| Causal re-run of the real header on cost dumps; full-stream counts and time (`stream=1`) | `scripts/analysis/t1_csim.cpp` |
| Reordered interwiki proxies (pages with interlanguage tails, text stubbed, full-run order) | `scripts/analysis/t1_proxy.py` |
| Full-run per-language rates before a proxy (warm seed) | `scripts/analysis/t1_seed.py` |
| Per-rank-bin counts in the full run's order; projection | `build/exp/T1/rank_bins.py`, `scripts/analysis/t1_project.py` |

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic; T=build/exp/T1; R=build/refs/fx2-cmix-transformer-v1
python3 -P -E scripts/analysis/t1_survey.py --pre build/exp/A1/full/full.pre                       # full counts
python3 -P -E scripts/analysis/t1_survey.py --pre build/exp/X1/run100M/100M.pre --cost p100M_cost.bin   # 42 MB costs
python3 -P -E scripts/analysis/t1_iw.py --pre build/exp/E2/proxy/10M.pre --cost cost_10M.bin
python3 -P -E scripts/analysis/t1_iw.py --pre build/exp/A1/full/full.pre
python3 -P -E scripts/analysis/t1_slots.py --pre build/exp/E2/proxy/10M.pre --cost build/exp/A1/runs/cost_10M.bin --dict $D \
  --out $T/slots_10M_np.bin --noparen 0.3
g++ -O2 -march=x86-64-v3 -o $T/t1_sim scripts/analysis/t1_sim.cpp && $T/t1_sim build/exp/A1/runs/cost_10M.bin $T/slots_10M_np.bin lr=0.004
g++ -O2 -march=x86-64-v3 -std=c++17 -I patches/exp/T1-fieldcopy -o $T/t1_csim scripts/analysis/t1_csim.cpp
$T/t1_csim build/exp/A1/runs/cost_10M.bin $D;  $T/t1_csim build/exp/A1/full/full.stream $D stream=1
python3 -P -E build/exp/T1/check_proxy.py build/exp/E2/proxy/10M.pre        # writes build/exp/T1/full_groups.pkl once
python3 -P -E scripts/analysis/t1_proxy.py --ctx 3 --ranks 120000:170000 --meta build/exp/X2/meta_pages.tsv --out $T/iwp_120k.xml
$R/slicetool pre $T/iwp_120k.xml $T/iwp_120k.pre                               # (+ slicetool post | cmp, check_proxy.py)
build/exp/T1/run_costlogs.sh 120k                                              # cost-logged record run, fast lane
python3 -P -E scripts/analysis/t1_seed.py --rank 120000 --meta build/exp/X2/meta_pages.tsv --out $T/seed_120k.txt
$T/t1_csim $T/run_120k/cost_iwp_120k.bin $D seed=$T/seed_120k.txt > $T/csim_warm_120k.txt
python3 -P -E scripts/analysis/t1_project.py --bins $T/rank_bins.tsv --point $T/iwp_5k.pre:$T/csim_warm_5k.txt ...
```
Costs come from the record's per-byte cost dumps (`patches/tools/per-byte-cost`); `t1_iw.py` and `t1_survey.py` use
X2's per-text-position caches (`build/exp/X2/tcost_*.npy`) or build their own in `build/exp/T1`.

## 1. Where the text sits in phda9's stream

The title line (`    <title>T</title>`) opens each page, before its text, so every in-article law has the title
in the transformer's window when the article starts (W = 1,024 tokens, reset per article). phda9 moves two things
out of the pages into blocks after the last page: the revision headers (X2's metadata block) and, after that, the
**interlanguage block**: for each article whose text ends in interlanguage links, everything from the first of
those links to the end of the text, `</text>` included, as one group ending in `</text>`:

```
...\n/contributor>\n[ar:لاسلطوية]\n[ast:Anarquismu]\n...[zh-min-nan:Hui-thóng-tī-chú-gī]</text>\n[de:Autismus]\n...
```
The article itself keeps no `</text>`, which makes the mapping decoder-valid: the k-th page without `</text>`
(and without an empty `<text xml:space="preserve" />`) owns the k-th group. [M] It holds on 1M (37/37), 10M
(747/747) and full enwik9 (93,700/93,700; 7 empty-text pages excluded), and the real model reproduces the full
count from the coded stream alone. The page order doesn't matter (the reordered full run included), and the block
carries no other pointer to its article, which is why X2 (§3.1) took it for unreachable. Groups can hold
arbitrary moved text (45 groups start with or contain non-link lines, some over 1,000 bytes), so group ends must be found by a
rolling `</text>` match, not a line test (the first build missed 13 groups this way and drifted).

## 2. Survey and pricing [M]

**In-article laws.** Full-enwik9 instances (identity order), the record's bits per instance on exactly the
derivable bytes from the first 42 MB (X1's partial 100M cost dump; census rows from mid10M), and stake = holds ×
bits. Stake is an UPPER BOUND on gain.

| law | instances | law holds | bits / hold | **stake** | note |
|---|---:|---:|---:|---:|---|
| bold restatement `'''T'''` in the first 600 B | 143,803 | 54,858 | 0.74 | **5.1 KB** | title is in the window |
| bold = T without " (...)" | | 2,997 | 0.56 | 0.2 KB | |
| `[Category:YYYY births]`, year in the lead | 19,688 | 19,305 | 2.01 | **4.9 KB** | the law only picks among lead years |
| `[Category:YYYY deaths]`, year in the lead | 13,940 | 13,671 | 2.02 | 3.5 KB | |
| first sort key = "Last, First" of T | 45,246 | 17,527 | 2.07 | **4.5 KB** | |
| first sort key = T | | 1,922 | 3.27 | 0.8 KB | |
| later sort keys (copies of the first) | 109,229 | 109,229 | 0.72 | 9.8 KB | already a copy for the record |
| infobox `name =` T | 4,326 | 2,436 | 0.94 | 0.3 KB | |
| `#REDIRECT` target = T modulo case/spaces (CamelCase redirects) | 70,318 | 5,795 | 5.83 | **4.2 KB** | |
| redirect target starts with T's first word | | 14,930 | 4.98 | (9.3 KB) | only part derivable |
| census: category = a link of the lead (`[Category:Jefferson County, Kentucky]`) | 30,950 pages | 25,908 | 0.53 | 1.7 KB | |
| census: category = "Cities/Towns/... in STATE" | | 32,019 | 0.56 | 2.2 KB | |
| `{{DEFAULTSORT:...}}` | 0 | | | 0 | not in the 2006 dump |
| **in-article total** | | | | **≈ 37 KB** (≈ 46 with the partial redirect law) | |

The record is already cheap on all of these: once the title is in the context, the transformer copies it (0.74 bits
for a bold restatement), and the CM's match models copy repeated keys. A specialist could take perhaps a third of
the stake (about 12 KB), and 10 KB of it is the copy-of-the-first-key row the record already handles: **NO-GO**.

**Interlanguage titles** (`t1_iw.py`). Each link's title X against the group's English title T and the group's
earlier links. Full counts [M]; record bits per link from the 10M slice [M] (the reordered proxies give 4.8–12.7
bits per eqT link, §4).

| class | full links | full bytes | 10M links | record bits / link (10M) | stake (full × 10M rate) |
|---|---:|---:|---:|---:|---:|
| **eqT** (X = T, not a copy of an earlier link) | **48,820** | 600,787 | 470 | **13.10** | **≈ 80 KB** |
| eqT_noparen (X = T without " (...)") | 1,835 | 14,776 | 11 | 13.32 | 3.1 KB |
| eqT_dia (equal after stripping diacritics) | 2,564 | 25,566 | 90 | 15.42 | 4.9 KB (not a candidate) |
| eqT_case | 318 | 3,070 | 2 | 14.91 | 0.6 KB |
| lcpT4 (≥ 4-byte common prefix with T) | 82,191 | 1,284,423 | 1,731 | 18.44 (5.06 on the prefix) | ≈ 52 KB on the prefix |
| copy (= an earlier link of the group) | 251,017 | 2,078,708 | 4,887 | 1.19 | 37 KB, already cheap |
| near_dia / other (new foreign titles) | 291,000 | 5.0 MB | 7,406 | 9.4 / 20.5 | ≈ 740 KB, not derivable |
| all | 677,744 | 9.0 MB | 14,597 | | |

Ranked by stake × removable share: **interlanguage eqT (≈ 80 KB) ≫ the lcp prefixes (≈ 52 KB, partly removable)
≫ every in-article law (≤ 10 KB)**. eqT is 3.2% of links in the first 10 MB but 7.2% in the full file: later articles
(people, places, bot-made Ido/Afrikaans/Danish stubs) carry the English title much more often, which is why slices
undersell the law. 52% of groups have an eqT link, and 71% of eqT links are a group's first link (de 16,264;
io 6,232; fr 3,800; da 2,909; af 2,448; ...).

## 3. The specialist and its sims [M]

At each link title (after `[xx:` at a line start, inside the block) one slot opens. Candidates: the coded bytes of
the owning page's title exactly as its title line coded them, plus `]` (weight 1), and the same for T without a
trailing ` (...)` (0.3). Working in coded space makes the copy exact without a WRT encoder; on every slice the
link's coding equals the title line's whenever the texts match. The slot covers the coded bytes up to the first one
that leaves every candidate (that byte is paid for) or the candidate's end. Class (decoder-valid, earlier links
only): group history (first link / an earlier link was T / none was) × the language's running rate of T links
(4 buckets): 12 classes. Mixing is E12/X2's final-stage mixer (stretch(p), bias, StateMap(p1), stretch(p1); a weight
set per class × state × bit), lr 0.004 (0.002: −553 B on 10M, 0.004: −578, 0.008: −571).

| variant (10M, offline sim `t1_sim`) | gain |
|---|---:|
| T only, lr 0.002 | +553 B |
| T only, lr 0.004 | +578 B |
| T + no-paren T (0.3), lr 0.004 (**chosen**) | +582 B |
| + the group's earlier link titles as candidates | +449 B (worse: the record already copies them) |

The real header (`t1_fieldcopy.h`) re-run causally on the same dump (`t1_csim`) gives **−586 B**, 14,600 slots
vs the sim's 14,597. Per class (10M): first links in languages with a 0.2–0.5 rate carry most of it (516 slots,
652 → 318 B, −5.2 bits per slot), then later links of groups with no T so far (−0.44 bits per slot, via prefixes);
links after a T link gain nothing (the record already copies T within the group) and lose ≤ 2 B.

## 4. Reordered interwiki proxies and the real build [M]

Slices hold only the first articles' groups, in identity order, and few eqT links. As X2 did for the metadata
block, `t1_proxy.py` writes enwik9's intro plus, in the full run's page order (`meta_pages.tsv`), the pages of a
rank window that own a group, each with its text replaced by `'''T'''` plus the original lines from 3 non-blank
lines above the interlanguage tail to the end (with a bare stub phda9 moves only part of the tail; 3 lines make
every group identical to the full run's: `check_proxy.py` compares each against `full.pre`'s). So the block is the
full run's block for that window, with the full run's local neighbours. All proxies round-trip through slicetool.
The record's cost log (`cmix_costlog`, observation only, fast lane, about 3 min) then gives its cost on that block.

Some windows (30K, 40K, 50K, 80K, 85K–95K, 160K, 165K with 3 context lines) fail the record's slice-mode vocabulary check
(a 0x00 byte after cmix's own preprocessing; the interwiki-only stream looks different enough to the preprocessor's
detector; full enwik9 is one text block, so the real run is unaffected). The windows used are the ones that pass:
5K, 45K, 120K and 155K with 3 lines; 85K needed 20 lines (339 groups).

| proxy (median full-run rank) | groups | eqT links | record bits per eqT link | T1 gain, cold | warm-seeded rates | per eqT link |
|---|---:|---:|---:|---:|---:|---:|
| 5K window (6,444) | 1,682 | 1,317 | 11.88 | −2,130 B | −2,149 B | 1.63 B |
| 45K window (46,285) | 1,609 | 995 | 11.99 | −1,479 B | −1,471 B | 1.48 B |
| 120K window (124,620) | 1,902 | 992 | 4.83 | −1,065 B | −1,096 B | 1.10 B |
| 155K window (165,564) | 1,730 | 762 | 12.74 | −1,147 B | −1,175 B | 1.54 B |
| 85K window, 20 lines (85,263) | 339 | 100 | 12.29 | −152 B | −161 B | 1.61 B |
| enwik9_10M (identity order, for comparison) | 747 | 470 | 13.10 | −586 B | | 1.25 B |

"Warm-seeded" pre-loads each language's T-link counts from all earlier groups of the full run (`t1_seed.py`); it
changes little because the rates settle within a few hundred groups.

**Real build** (`patches/exp/T1-fieldcopy`, C3 + T1 as a final stage after X2; `scripts/bench.sh` through
`run.sh`, cmp-verified round trips; ledger rows `fx2T+T1-v1` and the control `fx2T+C3-v1` on the same binary):

| input | C3 | C3 + T1 | gain | causal sim on record costs |
|---|---:|---:|---:|---:|
| enwik9_10M | 1,261,700 | **1,261,120** | **−580 B (−0.046%)** | −586 B |
| interwiki proxy, 120K window (`iwp_120k.xml`, 999,900 B) | 33,864 | **32,805** | **−1,059 B (−3.13%)** | −1,065 B |
| enwik9_1M (37 groups) | 126,244 | **126,234** | **−10 B** | −10 B |

Real = sim within 6 B (the sim runs on the record's costs, the build on C3's, whose E12/X2 stages don't touch the
block). C3's other stages and T1 don't overlap: T1's slots are all inside the interwiki block.

## 5. Projection [ESTIMATE]

`t1_project.py`: the gain per normaliser at each proxy, interpolated linearly in rank, times the full run's counts
per 10K-rank bin (`rank_bins.tsv`; 93,700 groups, 48,820 eqT links, all before rank 180K):

| normaliser | projected gain |
|---|---:|
| per eqT link (the law's direct target) | **72.0 KB** |
| per group | 74.1 KB |
| per eqT + lcpT4 link (prefix gains too) | 85.4 KB |
| (cold 10M per-class gains × full per-class slot counts, `t1_csim stream=1`) | (61.8 KB) |

**About 72 KB, range 48–90 KB.** The point uses the eqT normaliser (the gain sits on eqT links in every proxy);
the high end is the prefix-inclusive normaliser. The low end divides the point by 1.5 for the full run's warm CM, as X2 did:
the proxies are cold, and a full run's match models have 900 MB of history in which an English title's later
bytes may already be predicted once its first bytes are coded (the 120K window, where the record pays 4.8 bits per
eqT link instead of 12, shows the gain survives a cheaper record: 1.10 B per eqT link). Above the 30 KB bar either way.

## 6. Cost

- **Time:** the model alone runs at 0.04 µs per coded byte over the full 586 MB stream (24 s on this box), plus the
  mixer inside the 679K slots (about 4.5M coded bytes): **< 0.01 h** against the 7.8 h headroom.
- **RAM:** 19 MB peak on the full stream (93,700 queued coded titles and their no-paren variants, the language
  table, the dictionary map).
- **Size:** the binary grows from 604,536 to 620,952 B (+16.4 KB unpacked; PGO layout differs; not
  size-optimized; its WRT decoder and dictionary loader duplicate X2's). The packed cost for S still needs measuring.
- **Determinism:** integer state plus doubles for the mixer, the same binary in both directions, as E12/X2.

## 7. What next

1. Fold into the next combination and the full-mode build (the switch becomes a compile-time default reading the
   embedded dictionary, as E12/X2). Share the WRT decoder and dictionary with X2 to cut the binary growth.
2. Cheap extensions with stake left: diacritic-stripped and case variants as candidates (eqT_dia/case ≈ 5.5 KB at
   stake); a language-specific transliteration of the title for Latin-script languages (the "other" class holds
   ≈ 740 KB, but the record already pays only 1.3–1.9 bits per byte there).
3. The same trick may apply wherever phda9 splits a page: the metadata block's records also map to pages in order
   (X2 used the ids, not the titles); a per-record title-conditioned model of user names (bots that touched a page
   family) is untested.
