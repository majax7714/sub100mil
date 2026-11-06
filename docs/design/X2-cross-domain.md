# X2: cross-domain dart, round 2: domain laws as computation, and stratigraphy of the metadata block

**Verdict: GO, for an unexpected target.** Track B's text laws (ISBN check digits, chronological lists, ordinal
suffixes, table sums, calendar facts, Kepler, chemistry, taxoboxes; 30 candidates counted over full enwik9) are
small: about 150 KB at stake in all, and the best two simulate to **+327 B on 10M (≈ 14 KB projected)**. The dart that
hit is stratigraphy, Track A's geology entry: **Wikipedia revision ids are issued in time order**, so each page's
timestamp in phda9's metadata block is a monotone function of the revision id the block stores just before it, and
neither CM nor the transformer can interpolate that calibration curve. A causal specialist (time from the nearest
known revision ids, plus a small redirect page-id law) mixed E12-style with the record's per-bit probabilities gains
**+116 B on 10M and +4 B on 1M** (the block's cold start), **+1,332 B (−54% of timestamp bits) on census mid10M**, and
+1,112 to +2,329 B on 1 MB proxies of the full run's reordered block, measured with nine observation-only cost-logged
record runs. **Projected to full enwik9: about 127 KB, range 85–135 KB [ESTIMATE]**, for seconds of CPU, < 20 MB
of RAM and about 2 KB of S. It joins E12's final stage as two more slot classes, with no new inputs.
A tempting larger law (page id from the full run's article order, ≈ 165 KB simulated) is **not decoder-valid**: the
order file isn't in archive9, and those deltas are how the permutation gets to the decoder (§5.2, §7).

Classification: **model addition (non-transformer specialist, final stage)**. A preprocessing variant around the
permutation channel is a follow-up (§7). Nothing was added to the compressor. Ten observation-only, cost-logged record
compressions of 1 MB proxies were made (§5.4), with no archive kept and no round trip, so there are no ledger rows.

Labels as in the A1/X1 docs: **[M]** measured here, **[ESTIMATE]** derived from measurements by the argument next to it,
**UPPER BOUND** a ceiling, not an expected gain.

## 0. Question, method, reproduction

The brief had two tracks. Track A: mechanisms from fields X1 didn't cover (neuroscience, ecology, economics, historical
linguistics, cryptanalysis, geology, music, epidemiology, control), each mapped to bytes and checked against the record.
Track B: E12's lesson generalized. Wikipedia text holds values that a domain law fixes exactly (check digits, calendars,
sums, conversions), and neither CM nor a small transformer can compute them. Census arithmetic and inline
`N km (M mi)` conversions belong to E12 and are excluded. Every candidate is **counted in the data first**: occurrences in
full enwik9, and the record's bits on exactly the bytes the law fixes. Those bits at stake bound the gain.

| Step | Script |
|---|---|
| Target census: candidate spans (regex + law check), full-enwik9 counts, record bits per span | `scripts/analysis/x2_census.py` |
| Interlanguage-link block costs by script / language / copy | `scripts/analysis/x2_interwiki.py` |
| Metadata block costs per field | `scripts/analysis/x2_meta.py` |
| Revision-id → time calibration on the full block (specialist alone) | `scripts/analysis/x2_meta_interp.py` |
| Causal slots, text laws (ISBN check digit, ordinal suffix, chronological lists, year articles) | `scripts/analysis/x2_slots.py` |
| Causal slots, metadata laws in identity order (slices) | `scripts/analysis/x2_meta_slots.py` |
| The full run's page order (mirror of `article_reorder.h`) with each page's metadata and article number | `scripts/analysis/x2_reorder.py` |
| Metadata-heavy proxies of the reordered block (text stubbed) for cost-logged record runs | `scripts/analysis/x2_proxy.py` |
| Metadata laws in the full run's (reordered) block: report and causal slots | `scripts/analysis/x2_meta2.py` |
| Per-bit mixing simulation (`e12_sim.cpp` with X2's class names; method unchanged) | `scripts/analysis/x2_sim.cpp` |
| Projection over the full block | `scripts/analysis/x2_meta_project.py` |

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic; X=build/exp/X2; R=build/refs/fx2-cmix-transformer-v1
# census: full counts, record bits on the 10M slice and on the 42 MB prefix of X1's 100M cost log
python3 -P -E scripts/analysis/x2_census.py --pre build/exp/A1/full/full.pre --out $X/census_full.tsv
python3 -P -E scripts/analysis/x2_census.py --pre build/exp/E2/proxy/10M.pre --cost build/exp/A1/runs/cost_10M.bin --dict $D --null --out $X/census_10M.tsv
python3 -P -E scripts/analysis/x2_census.py --pre build/exp/X1/run100M/100M.pre --cost build/exp/X1/p100M_cost.bin --dict $D --null --out $X/census_p42M.tsv
# text-law sims
g++ -O2 -march=x86-64-v3 -o $X/x2_sim scripts/analysis/x2_sim.cpp
python3 -P -E scripts/analysis/x2_slots.py --pre build/exp/E2/proxy/10M.pre --cost build/exp/A1/runs/cost_10M.bin --dict $D --out $X/slots_10M.bin
$X/x2_sim build/exp/A1/runs/cost_10M.bin $X/slots_10M.bin
# metadata block: identity-order slices
python3 -P -E scripts/analysis/x2_meta.py --pre build/exp/E12/mid10M.pre --cost build/exp/E12/runs/cost_mid10M.bin
python3 -P -E scripts/analysis/x2_meta_slots.py --pre build/exp/E12/mid10M.pre --cost build/exp/E12/runs/cost_mid10M.bin --dict $D --out $X/mslots_mid10M.bin --fields ts,uid
$X/x2_sim build/exp/E12/runs/cost_mid10M.bin $X/mslots_mid10M.bin
# metadata block in the full run's order: proxies, record cost logs, sims, projection
python3 -P -E scripts/analysis/x2_reorder.py --out $X/meta_pages.tsv
python3 -P -E scripts/analysis/x2_meta2.py --meta $X/meta_pages.tsv --report
python3 -P -E scripts/analysis/x2_proxy.py --ranks 100000:102800 --meta $X/meta_pages.tsv --out $X/proxy_s.xml --strip-comments
$R/slicetool pre $X/proxy_s.xml $X/proxy_s.pre          # (slicetool post round-trips every proxy)
mkdir -p $X/run_s && cd $X/run_s && ../../../../scripts/slot.sh --fast env FX2_COSTLOG=$PWD/cost_proxy_s.bin \
  ../../../../build/exp/A1/cmix_costlog -c ../../../../$D ../proxy_s.pre proxy_s.cmix --transformer ../../../../$R/tree/models/6m-q4-fp32.tfwc2; cd -
python3 -P -E scripts/analysis/x2_meta2.py --meta $X/meta_pages.tsv --pre $X/proxy_s.pre --cost $X/run_s/cost_proxy_s.bin --dict $D --out $X/m2slots_proxy_s.bin
$X/x2_sim $X/run_s/cost_proxy_s.bin $X/m2slots_proxy_s.bin
python3 -P -E scripts/analysis/x2_meta_project.py --meta $X/meta_pages.tsv --point 1400:... --point 101400:$X/m2sim_proxy_s.txt ...
```
(Python runs as `python3 -P -E`: numpy lives in the user site-packages. The other proxies (ranks 5000, 40000, 70000, 130000,
160000, 175000, 200000, 230000; about 1 MB each, so they run in the fast lane in 2.5 min) and the identity-order control
`--by-page --ranks 120000:122850` are built and run the same way. The projection reads the nine sims:
`x2_meta_project.py --point <mid rank>:<sim.txt> ...`, output in `build/exp/X2/projection.txt`.)

## 1. Track A: mechanisms from fields X1 didn't cover

"Covered?" is the blunt check against the record (cmix CM + PPMd + match models + SSE + 23 mixers + a frozen int4
transformer trained 592 epochs on enwik9, so global statistics are in its weights). Target mass is the record's bits on
the bytes the mechanism acts on, full enwik9, from §2 where measured.

| # | Field: mechanism | Byte-level analogue | Covered? | Target mass / note |
|---|---|---|---|---|
| A1 | Neuroscience: **predictive coding** (only prediction errors travel up a hierarchy) | Code the residual of a higher-level prediction | **Yes**: SSE/APM chains, E2's correction head | — |
| A2 | Neuroscience: **hippocampal replay / consolidation** (replay recent experience offline into slow learners) | Re-train the online learners (E2 head, mixers) on a buffer of recent hard bytes, encoder and decoder alike | **No** in the record; E10 dyneval and E2 take the same signal | Not testable from cost dumps (needs the head's inputs); bounded by the E2/E10 gains (≈0.2%) and the 7.8 h headroom. Not tested |
| A3 | Neuroscience: **dendritic gating** (context selects which synapses count; gated linear networks were modelled on it) | Context-selected weight sets | **Yes**: mixer selectors; X1's coarse-state APM gave ≤ 28 B on 10M | — |
| A4 | Ecology: **species abundance, Pitman–Yor / Ewens sampling** (probability of a new species) | Novel-token (first use) probability | **Yes**: PPMd escapes, transformer; X1/E11 show first uses are where the record is already best | — |
| A5 | Ecology: **capture–recapture** (estimate a hidden population size from overlapping samples) | Infer a hidden denominator from (count, %) pairs: election boxes, vote tables, percentages of a common total | Census lattices are E12. Outside census, **no**, but tiny | election boxes 136, table `pct` laws ~0.9 KB stake [M] |
| A6 | Economics: **prediction markets / proper scoring rules** (log-score market maker) | Mixer as a market of experts | **Yes**: logistic mixing is a log-score pool | — |
| A7 | Economics: **double-entry bookkeeping** (every total equals its parts) | Table cells that are sums or differences of earlier cells (medal totals, Pld = W + D + L, goal difference) | **No** (CM can't add), but small | sum/diff/pld/pts ~14 KB stake [M] |
| A8 | Historical linguistics: **sound laws** (regular correspondences between languages) | Predict an interlanguage link's title from the English title or other languages' titles by learned transliteration | **Partly**: the memorized transformer already pays only 1.2–1.5 bits per UTF-8 byte; phda9 moves the links to a block after all pages, with no pointer back to the article | interlanguage block 1.9% of 10M; non-Latin 256 KB, Latin-extended 182 KB stake [M]. See §3.1 |
| A9 | Philology: **stemmatics** (reconstruct an archetype from copies with errors) | Bot-generated article families as copies of a template | **Yes**: match models, A1 (NO-GO), E12 for census | — |
| A10 | Cryptanalysis: **checksums / known plaintext** | ISBN check digits (`Σ i·dᵢ ≡ 0 mod 11`), ISSN | **No** | ISBN-10 13.8 KB stake. **Tested (§4)** |
| A11 | Cryptanalysis: **frequency analysis, Kasiski periods** | Period detection in tables | **Yes**: column context (X1 P6) | — |
| A12 | Geology: **stratigraphy and radiometric dating** (layers are deposited in order; a calibration curve maps a measured quantity to an age) | (a) Wikipedia revision ids are issued in time order: time is a monotone function of the id. (b) Page ids grow with the article number of the full run's order file. (c) Chronological lists are sorted (superposition) | (a) **No**: neither CM nor the transformer can interpolate a calibration curve. (b) Not available: archive9 doesn't carry the order file (§5.2). (c) Partly | **metadata block: about 1.6 MB of the full archive (all fields); time from revision id ≈ 210 KB at stake** [M, §5]. **Tested (§5): GO**, about 127 KB. Chronological lists 50 KB stake, tested (§4) |
| A13 | Music theory: **meter** (periodic stress) | Periodic structure in tables and lists | **Yes**: column context | — |
| A14 | Epidemiology: **SIR contagion** | Bursts of a term across consecutive articles | **Yes**: E11/X1 recency caches, CM adaptation | — |
| A15 | Epidemiology/demography: **life tables** | Ages from birth/death years ("died … aged 80") | **No**, but tiny | 721 instances, 0.6 KB stake [M] |
| A16 | Control theory: **PID / gain scheduling** of a learning rate | Feedback-controlled mixer learning rates | Essentially: A3 found the LR optima flat | — |

## 2. Track B: domain laws as computation (target census) [M]

`x2_census.py`. Counts are over **full enwik9** (`full.pre`). Bits per instance are the record's cost on exactly the
derivable bytes, measured on the first 42 MB of enwik9 (X1's 100M cost log prefix, 24.7M coded bytes; the
interlanguage block, which sits after all pages, from the 10M slice). **Stake = full-enwik9 instances where the law
holds × bits per instance**, an UPPER BOUND on any specialist's gain. Rambot census articles are skipped for the numeric
laws (E12's territory). "numlaw" rows are a generic miner: a number that equals a sum/difference/percentage/ratio/unit
conversion of the ≤ 8 numbers before it in the same table row, infobox or line; they're reported **net of a null**
(the same test against the context of a random other record), because coincidences are common (58% for loose
conversions).

| candidate (law) | full instances | law holds | bits / instance | **stake** |
|---|---:|---:|---:|---:|
| **metadata block, time from revision id** (§5) | 243,426 records | 154,990 slots (rev id ≥ 18.1M) | 10.8–15.7 | **≈ 210 KB** |
| metadata block, page-id delta from article number (reordered block; **not decoder-valid**, §5.2) | 243,426 | 172,174 slots | 6.7–12.6 | ≈ 200 KB (it's the permutation's channel) |
| metadata block, redirect page id = next unused id (§5) | 243,426 | 71,149 slots | 3.1–3.6 | ≈ 30 KB |
| interlanguage link titles, ASCII (A8) | 415,903 | — | 7.6 | 397 KB |
| interlanguage link titles, non-Latin script (A8) | 96,145 | — | 21.3 | 256 KB |
| interlanguage link titles, Latin with diacritics (A8) | 76,489 | — | 19.1 | 182 KB |
| chronological list year ≥ previous year (A12c) | 110,596 | 101,946 (92%) | 3.92 | 50.0 KB |
| repeated date, same format | 33,229 | — | 6.03 | 25.0 KB (match models already copy) |
| ordinal suffix from the number (`21st`) | 149,710 | 99.9% | 1.11 | 20.8 KB |
| numlaw: unit conversion (non-inline, net of null) | ≈ 22,000 | ≈ 15,400 | 9.87 | 19.0 KB (part E12) |
| ISBN-10 check digit (A10) | 32,708 | 99.0% | 3.40 | 13.8 KB |
| coordinate templates outside census (whole template) | 2,707 | — | 28.3 | 9.6 KB |
| numlaw: sum of preceding cells (A7) | ≈ 16,400 | ≈ 12,000 net | 4.61 | 6.9 KB |
| numlaw: difference (goal difference, majority) | ≈ 13,900 | ≈ 10,000 net | 4.15 | 5.2 KB |
| repeated date in a new format (`{birth date|1940|3|3}` → `March 3, 1940`) | 2,195 | — | 13.95 | 3.8 KB |
| numlaw: ratio (densities, averages) | ≈ 2,000 | ≈ 1,800 net | 10.98 | 2.5 KB |
| numlaw: L = Pld − W − D | ≈ 3,200 | ≈ 2,400 net | 4.17 | 1.3 KB |
| numlaw: percentage of two cells | ≈ 1,650 | ≈ 900 net | 7.94 | 0.9 KB |
| percentages summing to 100 (last item) | 5,577 lines | 879 (16%) | 6.53 | 0.7 KB |
| age from birth/death years (A15) | 2,429 | 721 (30%) | 6.73 | 0.6 KB |
| year after a weekday (`Monday, March 3, 19xx`: 1/7 of years fit) | 1,196 | 97% | 3.16 | 0.5 KB (≤ 2.8 bits of it derivable) |
| win–loss percentage `.586` | ≈ 350 | ≈ 240 net | 13.7 | 0.4 KB |
| date articles: "Nth day of the year … N days remaining" | 366 articles | 100% | 7.45 | 0.3 KB |
| Pts = 3W + D / 2W + D | ≈ 620 | ≈ 530 net | 3.00 | 0.2 KB |
| orbital elements (Kepler, perihelion = a(1−e)) | 101 fields | — | 15.8 | 0.2 KB |
| taxobox binomial = genus + species | 1,237 | 98% | 1.32 | 0.2 KB |
| election box % / majority / turnout (A5) | 165 | 82% | 10.6 | 0.2 KB |
| chembox molar mass from formula | 41 | — | 25.7 | 0.1 KB |
| ISSN check digit | 122 | 97% | 4.42 | 0.1 KB |
| year articles: Roman numeral, weekday of 1 January | 121 / 2 | 100% | — | ≈ 0.1 KB |
| Roman ↔ Arabic pairs, `(N years)` durations, weekday after a date, ISBN-13 | 875 / 52 / 16 / 30 | low | — | ≈ 0 |

**Reading.** The 2006 dump is thin on domain-law content outside the census. Chemboxes (41 molar masses), planet and
asteroid infoboxes (101 orbital fields), year articles (121 with a Roman numeral) and sports tables are rare, and the
other bot-generated families that could replay E12 (India census, Japanese municipalities, Polish villages) aren't in
the dump yet. The text laws together hold about 150 KB of record bits, most of it in chronological lists and
ordinals, where the law fixes only part of the value. **The big deterministic structure isn't in the article text. It's
in the metadata block** that phda9 builds after the last page: 243K records of page id, revision id, timestamp and
contributor. In the full run's order the record codes it at 34–79 bits per page, depending on the region, about 1.6 MB in all (§5).

## 3. Ranking

By bits at stake × the share a law can actually remove:
1. **Metadata block calibration laws.** Time from revision id: ≈ 210 KB at stake, exact up to interpolation. The page-id
   law (≈ 200 KB) turned out not to be decoder-valid (§5.2). **Tested (§5)**.
2. Interlanguage titles (≈ 835 KB at stake), but the record already pays only 1.2–1.5 bits per UTF-8 byte and a
   transliterator can't see the English title (§3.1). Not tested.
3. ISBN check digit: small stake (13.8 KB) but nearly all removable. **Tested (§4)**.
4. Chronological lists: 50 KB stake, the law is a lower bound only. **Tested (§4)**.
5. Ordinal suffix: 20.8 KB, deterministic given that a suffix follows. **Tested (§4)**.
6. Generic numeric laws (A7): ≈ 35 KB net stake, noisy. Not tested.

### 3.1 Interlanguage block (A8, historical linguistics) [M, 10M]

`x2_interwiki.py`: 13,867 links in 3,232 article groups cost 24,136 B, 1.91% of the 10M record. New titles (not a copy
or near copy of an earlier title in the same group) are 89% of it, at 1.13 bits per byte (ASCII), 1.65 (Latin with
diacritics) and 1.22 (non-Latin; ru 0.73, uk 0.60, ja 1.48, ko 2.66). Copies cost 0.28 bits per byte. The memorized
transformer already transliterates well, and the block carries no pointer to its article, so a sound-law model would
have to recover the English title first. Not pursued.

## 4. Test 1: text laws (ISBN check digit, ordinal suffix, chronological lists) [M]

`x2_slots.py` opens slots on **left-context triggers only**, so false triggers are paid for:
- **ISBN:** after `ISBN` and 9 digits (single `-` or space allowed between them). The candidates are the check
  character, `-` + check and ` ` + check (weights 0.7/0.25/0.05).
- **Ordinal:** after every digit of a free-standing digit run (≤ 4 digits), the run's suffix (`st/nd/rd/th`). The
  specialist abstains once a digit follows instead.
- **Chronology:** at the start of a list line whose previous list line (≤ 3 lines back, no heading in between) began
  with a 4-digit year y₀: years y ≥ y₀ weighted (1 + y − y₀)^−α, and a small weight down to y₀ − 50.

The mixing is E12's (`x2_sim.cpp` = `e12_sim.cpp`: logistic mixer over stretch(p_record), a bias, an adaptive StateMap
of the specialist's p1 and stretch(p1); a weight set per class × specialist state × bit). The baseline is the same mixer
with the record alone, so recalibration isn't credited.

| slice | class | slots | record B in slots | gain |
|---|---|---:|---:|---:|
| 1M | ISBN | 56 | 25 | +9 B |
| 1M | ordinal | 12,937 | 2,540 | −0 B |
| 1M | chronology | 2 | 1 | −0 B |
| **1M** | **all** | | | **+8 B** |
| 10M | ISBN | 518 | 226 | **+168 B** (3.48 → 0.89 bits per ISBN) |
| 10M | ordinal | 154,853 | 30,233 | **−11 B** |
| 10M | chronology (α = 1; 0.6 / 1.5 / 2: +171 / +168 / +167) | 5,987 | 2,963 | **+170 B** (3.96 → 3.69 bits per year) |
| **10M** | **all** | | | **+327 B (0.026%)** |

- The **ordinal suffix** is worth nothing. Once a letter follows the digits the record already knows which one;
  its cost is the decision *whether* a suffix follows, which the law doesn't fix.
- **Chronological lists** give 6% of their stake. The record already expects years to rise from the previous item; the
  law only excludes the past.
- **ISBN** removes 74% of its stake, but the stake is small.

**Projection [ESTIMATE]**, scaling the 10M gains by full/10M instance counts: ISBN 32,708/514 × 168 B ≈ **10.7 KB**,
chronology 110,596/5,987 × 170 B ≈ **3.1 KB**, ordinal 0. About 14 KB in total: **NO-GO for the text laws**, below the 50 KB
bar even together. The census table (§2) says the remaining text laws can't change that: their stakes are 0.1–7 KB each.

## 5. Test 2: the metadata block (stratigraphy and radiometric dating, A12) [M]

### 5.1 What the block is

phda9 moves every page's `<revision>` header into one block after the last page, one record per page:

```
>-31                      page id as a delta to the previous record's page id
revision>
id>38597768               revision id
timestamp>0537:37578      "0" + (year − 2001) + (31·(month − 1) + day − 1) ":" seconds of the day (UTC)
contributor>
username>Gene Nygaard     (or ip>…)
id>146986                 user id
/contributor>
```

The record pays **34–79 bits per record in the full run's page order** (§5.4; the reordered article part at ranks
5K–100K costs 74–79 bits, the later article clusters 34–51, and the redirects that come last 40–45), about 1.6 MB of the archive
for 243,426 records [ESTIMATE from the nine proxies]. Fields on the reordered proxy at rank 100K (2,791 records):
page-id delta 11.9 bits, revision id 22.6, timestamp 11.2 (day 0.2, seconds 10.9), user name 20.8, user id 9.8, IP 28.2.

### 5.2 The laws, checked on the whole block

- **Time from revision id (the GO).** Wikipedia issues revision ids in time order. Above 18.1M (154,990 records)
  the time is monotone in the id: after sorting by id, 0.0–0.7% adjacent inversions per 2M-id band. Below that (14.0M–18.1M,
  88,332 records), the 2005 import renumbered old revisions in page order, so their times are scrambled (35–38%
  inversions) and the specialist abstains there. A causal interpolation between the nearest known ids below and above
  (all earlier records of the block) has a median error of 8 s. Only already-decoded records are used, so it's
  decoder-valid in any page order.
- **Page id from article number: NOT decoder-valid.** The full run (`cmix -e`) reorders articles by similarity
  before phda9 (`article_reorder.h`: reorder, then `phda9_prepr` on `.main_reordered`), so the block's page-id deltas
  jump (">265476", ">-486"). Page ids grow with the order file's article number, and given that number a specialist
  pins the id to 2.1 bits (the record pays 6.7–12.6), which would be ≈ 165 KB in simulation. **But `archive9` doesn't carry the
  order file.** `selfextract_decomp` unpacks only the dictionary, the weights and the payload, and the decoder restores
  the original order with `sort()` by page id. The page-id deltas *are* the channel that transmits the permutation. Using this law
  would mean shipping the 201,077-byte compressed order file in archive9 as well, which costs more than it saves. Kept in
  `x2_meta2.py` for reference, not counted (§7).
- **Redirect page id = next unused id** (`rpid`, decoder-valid). The 71,149 pages that follow the reordered articles
  are redirects in id order; by then every article id is known, so a redirect's id is most likely one of the next
  unused ids. Small: 3.1–3.6 → 2.1–2.8 bits.
- **User id from user name** (deterministic). A warm CM already learns it after one occurrence (the 10M slice: 0.05
  bits on repeats), so it isn't counted. The renumbered-id chain (+1: 43%, +2: 19%, +3: 11% in id order) is already
  exploited by the record (+1 costs it 1.19 bits on 10M), and a user name matches its nearest-id neighbour's in only 39% of
  adjacent-id cases. Neither is counted.

Specialist alone on the full reordered block (`x2_meta2.py --report`, causal, Laplace scale adapted per neighbour-gap
bucket, window ≤ ±2,000): time **4.79 bits** per timestamp (99.3% in the window; 6.2 bits in the first tenth of the
block, 4.1–5.4 later), against the record's 10.8–15.7.

### 5.3 Identity-order slices (as the brief asks: 1M / 10M)

`x2_meta_slots.py` (time and user-id laws plus the renumbered chain; page ids are trivial in id order). The
specialist's knowledge is exactly a full run's: the slice's records are located in the full block, and everything
before them is known.

| slice | records | class | slots | record bits/slot | with specialist | gain |
|---|---:|---|---:|---:|---:|---:|
| 1M | 166 | revid chain / time / uid | 164 / 50 / 49 | 9.39 / 19.86 / 0.04 | 9.33 / 19.23 / 0.04 | **+4 B** (the block's start: nothing known yet) |
| 10M | 1,307 | revid chain / time / uid | 1,305 / 875 / 434 | 16.67 / 12.17 / 0.05 | 16.59 / 11.22 / 0.04 | **+116 B** (+12 / +103 / +0) |
| census mid10M | 2,771 (block ranks 90,067–92,837) | time | 1,514 | 13.11 | **6.06** | **+1,332 B** |
| census mid10M | | uid | 2,372 | 2.50 | 0.15 | +697 B (cold-CM artefact, not counted) |

Slices hold only the start of the block, where the specialist knows little. The census slice sits mid-block, and
there the time law already removes 54% of the timestamp bits.

### 5.4 The full run's block: proxies [M] and projection [ESTIMATE]

Slices can't show the full-run block: it's in reordered order and 243K records long. So `x2_reorder.py` mirrors
`article_reorder.h` (non-redirect article numbers from `new_article_order` are remapped to pages, then the
unused pages, i.e. the redirects, follow in id order), and `x2_proxy.py` writes enwik9's intro plus the pages of nine rank windows of that
order, with each page's text replaced by a one-line stub (and comments dropped) so that about 2,800 records fit in 1 MB.
`slicetool pre` turns each proxy into the full run's block for those ranks (all nine round-trip through `slicetool
post`), and `cmix_costlog` (the record plus A1's observation-only cost log, fast lane, about 2.5 min each) gives the
record's per-byte cost. The specialist's state at the window's first record is exactly the full run's: every earlier
record of the reordered block is known.

| proxy ranks | records | record metadata bits/page | time slots | record bits/ts | with specialist | gain | redirect-id slots: record → specialist |
|---|---:|---:|---:|---:|---:|---:|---|
| 5,000–7,800 | 2,789 | 78.5 | 2,757 | 10.77 | 7.54 | +1,112 B | — |
| 40,000–42,772 | 2,772 | 79.2 | 2,733 | 10.76 | 4.83 | +2,028 B | — |
| 70,000–72,790 | 2,790 | 73.6 | 2,708 | 11.61 | 5.02 | +2,230 B | — |
| 100,000–102,800 | 2,791 | 76.7 | 2,763 | 11.08 | 4.34 | +2,329 B | — |
| 130,000–132,800 | 2,800 | 50.9 | 2,558 | 12.80 | 5.55 | +2,321 B | — |
| 160,000–162,450 | 2,414 | 34.2 | 458 | 13.47 | 6.62 | +391 B | — |
| 175,000–177,795 (redirects) | 2,795 | 39.7 | 393 | 14.94 | 7.39 | +371 B | 3.09 → 2.08 (+352 B) |
| 200,000–202,800 (redirects) | 2,800 | 44.9 | 404 | 15.21 | 7.60 | +383 B | 3.43 → 2.63 (+281 B) |
| 230,000–232,800 (redirects) | 2,800 | 41.7 | 317 | 15.72 | 7.56 | +322 B | 3.58 → 2.81 (+268 B) |
| identity-order control: pages 120,000–122,850 in id order | 2,844 | 63.3 | 1,908 | 11.83 | 5.08 | +1,610 B | (page-id delta 1.14 bits) |

The time law takes **3.2 bits per timestamp at the block's start and 5.9–8.1 bits from rank 40K on**, i.e. 55–60% of
the record's timestamp bits once the calibration curve is dense. It's the same in id order (the control: 6.75 bits) and on the
census slice (7.04). **Warm-up check:** inside each block the record's own timestamp cost falls by 1.3–3.4 bits from
the first to the last third (it learns the format), but the gain doesn't: 7.78/6.77/6.58 bits per third on mid10M,
6.92/6.54/6.77 at rank 100K, 2.90/3.33/3.45 at rank 5K, 6.88/6.63/6.74 on the id-order control. What the record learns
isn't what the law knows.

**Projection [ESTIMATE]** (`x2_meta_project.py`): the gain per slot from each proxy, interpolated linearly in rank and
summed over the slots the causal walk opens in the full block:

| law | slots in the full block | projected gain |
|---|---:|---:|
| **time from revision id** | 154,990 | **119.5 KB** |
| redirect page id | 71,149 | 7.4 KB |
| **decoder-valid total** | | **≈ 127 KB** |
| (page id from article number: needs the order file in archive9, not counted) | 172,174 | (165.4 KB) |

**Range 85–135 KB.** The low end divides by 1.5 (`--deflate 1.5`: 84.7 KB). That allows for a full run's record, with 100K+ metadata records of
warm-up instead of 2,800, using long or sparse contexts that reach back to the revision-id digits and so predicting part of
the timestamp itself. The within-block trend doesn't show that, but the proxies can't rule it out at 50× the warm-up. The
high end allows for an untuned specialist (lr 0.004 gave E12 +3%; one-sided extrapolation and a better scale model are
missing). Both ends are above the 50 KB bar.

## 6. GO sketch: joining E12's final stage (not built)

E12 (`patches/exp/E12-arith/`) is a causal final stage after the record's SSE. It runs its own WRT decoder over the
coded bytes, opens slots on left-context triggers, follows candidate strings bit by bit, and mixes them with
`E12Mix` (weight set per class × specialist state × bit). The metadata specialist fits that frame unchanged:

1. **Block detection.** The metadata block follows the last `  </page>` of the main part. Its records have fixed tags:
   after `revision>\nid>` the model parses the revision id (no slot); `timestamp>` opens a **time slot** when that id is
   ≥ 18.1M; a `>` at a line start after `/contributor>\n` opens a **redirect page-id slot** once the article part is
   over (the first page-id delta whose page id lies below an earlier record's is no signal; simplest is to open it for every record
   and let the mixer's per-state weights learn where it helps, or to count records and start at 172,277).
2. **State.** A `std::map<int32,int64>` from revision id to time (≤ 243K entries, about 12 MB with node overhead;
   a sorted vector with batched merges would do too), the set of page ids seen (a 300K-bit bitmap), the previous
   record's page id, and a 31-bucket adaptive Laplace scale (running mean of |error| per log2 neighbour gap).
3. **Candidates.** Time: integer seconds within ±min(2,000, 8·scale) of the interpolation, rendered as
   `"0%d%d:%d\n"` ("0", year − 2001, 31·(month − 1) + day − 1, ":", seconds of the day), Laplace-weighted. Redirect
   id: the next 64 unused ids as `"%d\n"` deltas, weighted (1 + j)^−1.5. Sorted, so E12's lo/hi narrowing works as is.
4. **Mixing.** Two new classes in `E12Mix` (state per class × deterministic-0/1/both/abstain × bit), lr 0.002 as in
   E12 (try 0.004). Nothing is stored in the archive, and no new input is needed: no order file, no tables.

**Cost [ESTIMATE]:** at most two slots per page, 243K pages; typical windows of tens to hundreds of candidates
(≤ 4,001), so about 10⁹ simple operations over the whole run: **seconds** (< 0.01 h) against the 7.8 h headroom. RAM
≤ 20 MB (the record has about 0.3 GB of headroom). S: about +3–5 KB of uncompressed code (+1–2 KB packed), no tables.
Determinism: integer arithmetic plus doubles for the interpolation and the scale, the same binary in both
directions, as E12.

**Verification plan.** The law works in slice mode too (it needs no order file), so: (a) build it into E12's tree and
bench the 1 MB proxies here through `scripts/slot.sh --fast scripts/bench.sh` (round trip + ledger row; the sims
predict −1,112 B at rank 5K, −2,329 B at rank 100K); (b) the 100M slice (its block has 12K records, identity
order); (c) a full comp9/archive9 round trip, together with E12's planned full-mode run, which is the real test of
the projection.

## 7. Side findings

- **The page-id deltas are where the record pays for the reorder's permutation.** The record pays 6.7–12.6 bits per
  reordered article on the `>delta` field (≈ 200 KB over 172K articles [ESTIMATE]), while cmix compresses the order
  file itself to 201,077 B (9.3 bits per article) and only comp9 carries it. Two follow-ups this suggests, untested:
  (a) a cheaper in-stream coding of the permutation (a model of the deltas from the decoded titles; similar articles'
  ids are often close, ">-31"); (b) ship the order file in archive9 (+201 KB) and code the metadata block in **id order**:
  then the page-id delta costs 1.14 bits instead of ≈ 12, and in the id-order control the revision id costs 17.4
  bits against 22.5 reordered, the timestamp 14.3 against 11.2. Different page sets, so this is indicative only: a net of
  perhaps +50–100 KB, or nothing. It needs a same-page-set comparison first.
- **Interlanguage block:** 1.9% of the 10M record, about 835 KB at full scale at stake, but already at 1.2–1.5 bits per UTF-8 byte (§3.1).
- **Census-style families beyond Rambot don't exist in this dump** (India census, Japanese municipalities and Polish
  villages came later), and the text's domain laws are small (§2).
- **slicetool handles reordered and stubbed page sets** (all ten proxies round-trip byte-exactly), so `x2_proxy.py` is
  a cheap way to get the record's cost on any part of the full run's stream within the fast lane.

## 8. What next

1. ~~Build the time law~~ done (§9, `patches/exp/X2-timelaw`). Fold it into C2 and include it in E12's full-mode run.
2. Warm-up check of the projection's low end: the 30K-record proxy (`build/exp/X2/proxy_30k.pre`, ranks 100K–130K,
   about 5M coded bytes, main lane, about 45 min) was built but not run, since the main lane was busy. Compare the
   gain per slot in its first and last 3K records.
3. The permutation channel (§7): a same-page-set comparison of the metadata block in id order vs reordered order, then
   decide between modelling the deltas and shipping the order file.

## 9. Real build: `patches/exp/X2-timelaw` (round trips) [M]

The time law and the redirect page-id law are built as a sibling final stage after E12's
(`patches/exp/X2-timelaw/`: `x2_timelaw.h`, one predictor patch on top of E12-arith, `build.sh`; README there).
`X2_DICT` turns it on, `E12_DICT` E12, and with neither set the binary is the record. All rows go through
`scripts/slot.sh --fast scripts/bench.sh` with `slice_cmd.sh` (`slicetool pre` → cmix → `slicetool post`), with `cmp` against
the original input file. The proxies are ordinary enwik9-format XML (enwik9's 29-line intro, whole `<page>`s, the
closing tag; `x2_proxy.py`), so the standard slice pipeline takes them unchanged. Every row round-trips.

| input | record | E12 alone | E12 + X2 | **X2 vs E12** | causal sim (`x2_csim`, same header, cold) |
|---|---:|---:|---:|---:|---:|
| `enwik9_1M` | 126,473 (ledger) | 126,469 | 126,465 | **−4 B** | −4 B |
| reordered ranks 5,000–7,800 | 40,257 | 40,257 | 39,723 | **−534 B (−1.33%)** | −534 B |
| reordered ranks 100,000–102,800 | 37,358 | 37,358 | 36,804 | **−554 B (−1.48%)** | −554 B |
| reordered ranks 130,000–132,800 | 25,268 | 25,268 | 24,222 | **−1,046 B (−4.14%)** | −1,045 B |
| reordered ranks 200,000–202,800 (redirects) | 29,797 | 29,797 | 29,767 | **−30 B** | −30 B |
| identity-order control, pages 120,000–122,850 | 36,343 | 36,343 | 36,095 | **−248 B (−0.68%)** | −248 B |

- **Real = sim to the byte.** The causal sim runs the same header on the record's cost dumps, and agrees within 1 B on
  every file. E12 does nothing on these proxies (no census text), so the X2 column is X2's own gain.
- **Why the benches are smaller than §5.4:** a 1 MB proxy holds only its own ~2,800 records, spread over the whole
  revision-id range, so the calibration curve is sparse: 0.6–3.3 bits per timestamp. In a full run the decoder knows
  every earlier record of the block. `x2_csim seed=…` pre-loads exactly those (the full block's records before the
  proxy's first rank), and the same header then gives **3.18, 6.14, 6.68, 6.91, 7.12, 7.11, 7.71, 7.58, 8.11 bits per
  timestamp** at the nine proxy positions. The offline sim gave 3.23, 5.94, 6.59, 6.74, 7.26, 6.85, 7.55, 7.58, 8.13.
  The redirect law reproduces at ranks 200K/230K (0.79 vs 0.80/0.77 bits per slot), not at 175K (0.08 vs 1.01).
- **Cost:** +5 to +12 s per 1 MB proxy direction (contended timings; about 3,000 slots with up to 4,001 candidates each), 0 s
  on 1M; peak RSS +4 MB. On the full stream, ≤ 2 slots per page over 243K pages is the same order: well under 0.1 h.
  Binary 637,304 B vs E12's 612,664 (+24.6 KB uncompressed, unpacked; mostly the extra template instantiations;
  not optimized).

**Updated projection [ESTIMATE]**, from the real model's warm-seeded gains (`x2_meta_project.py` with the nine
`csim_warm_*.csim` points): time law **121.3 KB** + redirect ids **5.6 KB** = **≈ 127 KB, range 85–135 KB** (the low end is
/1.5 for a warmer record). That's unchanged from §5.4, now backed by the real model rather than the offline mixer.

**Warm-record check (30K-record proxy):** queued in the main lane (`build/exp/X2/run_30k`, cost-logged record run on
`proxy_30k.pre`, ranks 100K–130K), which was held by the C2/100M benches. When it lands:
`x2_csim build/exp/X2/run_30k/cost_proxy_30k.bin english.dic seed=build/exp/X2/seed_100000.tsv`, and compare the
gain per timestamp in the first and last 3K records.
