# E12: arithmetic-consistency specialist, design and evidence

**Verdict: GO.** A causal specialist that derives census numbers from numbers earlier in the same article (lattices
`k/P`, sums to 100, unit arithmetic, density ratios) and general unit conversions saves **12.9 KB on
`enwik9_mid10M` (3.8% of the record's 337,937 B)** in simulation, and the real cmix integration reproduces it on a
census slices: **337,982 → 325,041 B (−12,941, −3.83%) on 10 MB and 31,408 → 30,049 B (−4.33%) on 1 MB, round-trip verified** (§4). Projected to full enwik9: **about 170 KB, range 140–190 KB [ESTIMATE]**, above the 100 KB bar,
for about 0.1–0.3 h of CPU and under 1 MB of RAM. It is the first non-transformer mechanism above the bar.

Labels: **[M]** measured here; **[ESTIMATE]** derived by the argument next to it.
Classification: **model addition (non-transformer specialist)**.

## 0. Method and reproduction

| Step | What | Where |
|---|---|---|
| Per-byte costs | The record's cost dump on mid10M (new, `FX2_COSTLOG`, round-trip verified) and A1's 1M/10M dumps | `build/exp/E12/runs/cost_mid10M.bin`, `build/exp/A1/runs/cost_{1M,10M}.bin` |
| Offline slots | Regexes over the `slicetool pre` text find each derivable number and describe the specialist's candidate set (lists or lattices); text positions are mapped to coded indices with A1's verified WRT decoder | `scripts/analysis/e12_slots.py` |
| Offline sim | Candidates expanded and followed bit by bit, mixed with the record's per-bit probabilities (A2/E11 method, §2) | `scripts/analysis/e12_sim.cpp` |
| Causal sim | The real model (`arith.h`: own WRT decoder, left-context triggers only) on the same dumps; also counts slots and times the model on the full WRT stream | `scripts/analysis/e12_csim.cpp` |
| Projection | Full-enwik9 slot counts per (class, log2 denominator) × simulated gain per slot of the same cell | `scripts/analysis/e12_project.py` |
| Integration | Final stage after the record's SSE | `patches/exp/E12-arith/` |

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic; E=build/exp/E12
build/refs/fx2-cmix-transformer-v1/slicetool pre data/slices/enwik9_mid10M $E/mid10M.pre
python3 -P scripts/analysis/e12_slots.py --pre $E/mid10M.pre --cost $E/runs/cost_mid10M.bin --dict $D \
  --out $E/slots_mid10M.bin --audit $E/audit_mid10M.tsv                                    # 2 min
g++ -O2 -march=x86-64-v3 -o $E/e12_sim scripts/analysis/e12_sim.cpp
$E/e12_sim $E/runs/cost_mid10M.bin $E/slots_mid10M.bin bycd=1 > $E/sim_mid10M.txt          # 1 min
g++ -O2 -march=x86-64-v3 -std=c++17 -I patches/exp/E12-arith -o $E/e12_csim scripts/analysis/e12_csim.cpp
$E/e12_csim $E/runs/cost_mid10M.bin $D                                                     # causal, 1 min
$E/e12_csim build/exp/A1/full/full.stream $D stream=1                                      # full-scale counts + time
python3 -P scripts/analysis/e12_slots.py --pre build/exp/A1/full/full.pre --audit $E/audit_full.tsv --counts-only
python3 -P scripts/analysis/e12_project.py $E/sim_mid10M.txt $E/audit_full.tsv --extra $E/sim_10M.txt:conv
```
(`python3 -P`, not `-I`: numpy lives in the user site-packages, which `-I` hides.)

## 1. What is derivable [M]

enwik9 has about 36,000 Rambot census articles (36,376 "As of the [census]"). In the WRT stream digits, `.`, `,`, `%`,
`/`, `(` and `-` are literal single coded bytes, so a number is a contiguous run of coded bytes. Checked on mid10M
(2,683 census articles):

| relation | holds |
|---|---|
| every race / Hispanic percentage = round(100k/P, 2) for an integer k | 99.98% |
| every household percentage = round(100k/H, 1) | 99.9% |
| every age percentage = round(100k/P, 1) | 99.9% |
| sex ratio = round(100M/(P−M), 1) | 99.8% |
| average household size = round(P/H, 2) (no group quarters) | 50% |
| housing density (km² and mi²) = population density × units/people, within the rounding of the density | 88% (rest: rounding edges; the candidate range covers them) |
| mi² area = round(km²/2.589988, 1) | 90% |
| population density ≠ P/land(display): the source used unrounded areas, so only an interval (±0.7%) is known | — |

`k/P` is the big one: for a town of 500 people a two-decimal percentage can only take 1 in 20 display values.
The record (CM + a 6M-parameter transformer) cannot do this arithmetic.

## 2. The specialist and the mixing

Slot classes and candidate rules are in `patches/exp/E12-arith/README.md` and `arith.h`. Per bit, the specialist's
masses (m0, m1) over the candidates consistent with the bits coded so far give p1 = m1/(m0+m1) and a state
(only-0, only-1, both, abstain). A logistic mixer combines stretch(p_record), a bias, stretch(StateMap[class, q(p1),
byte position, bit]) and stretch(clamped p1), weight set per (class, state, bit), lr 0.002. **Baseline** = the same
mixer with only stretch(p_record) and the bias, weight set per (class, bit), so recalibration inside slots is not
credited (it is worth about +12 B). Outside slots both are the record.

The offline sim (`e12_slots.py`) found slots with whole-sentence regexes, i.e. it saw the right context. The causal
model (`arith.h`) opens slots on left-context triggers only, so it pays for false triggers (e.g. `with ` before
"no husband"). The two agree within 0.4% (§3).

## 3. Results on the slices [M]

| slice | record | offline sim gain | causal sim gain | of total |
|---|---:|---:|---:|---:|
| 1M | 126,430 B (coder sum) | — | +4 B | 0.003% |
| 10M | 1,264,639 B | +126 B | +127 B | 0.010% (one census article; the gain is unit conversions) |
| mid10M | 337,937 B | **+12,972 B** | **+12,928 B** | **3.83%** |

By class on mid10M (offline sim; the causal one matches within a few bytes per class):

| class | slots | record bytes | record bits/slot | with E12 bits/slot | gain B |
|---|---:|---:|---:|---:|---:|
| race% (k/P, 2 dec) | 15,943 | 11,139 | 5.59 | 4.60 | +1,974 |
| hdens_km (density × U/P) | 2,634 | 1,928 | 5.86 | 0.71 | +1,693 |
| hdens_mi | 2,634 | 1,690 | 5.13 | 0.71 | +1,453 |
| race%last (100 − sum) | 2,664 | 1,625 | 4.88 | 1.10 | +1,258 |
| hh% (k/H, 1 dec) | 10,714 | 8,535 | 6.37 | 5.43 | +1,256 |
| hh%cons (bounded by other hh%) | 5,356 | 3,748 | 5.60 | 4.71 | +593 |
| age% (k/P, 1 dec) | 10,656 | 7,875 | 5.91 | 5.52 | +517 |
| sexratio / sexratio18 | 2,692 / 2,663 | 2,591 / 2,264 | 7.70 / 6.80 | 6.38 / 5.47 | +443 / +442 |
| hisp% | 2,654 | 2,223 | 6.70 | 5.43 | +423 |
| dens_mi / dens_km (interval) | 2,633 / 2,583 | 1,981 / 2,101 | 6.02 / 6.51 | 4.71 / 5.21 | +430 / +418 |
| area_mi / land_mi (km² → mi²) | 2,668 / 2,652 | 575 / 455 | 1.72 / 1.37 | 0.50 / 0.43 | +407 / +314 |
| age%last | 2,664 | 1,068 | 3.21 | 2.01 | +399 |
| hhsize | 2,669 | 1,325 | 3.97 | 2.80 | +390 |
| water% / water_km / water_mi | 1,646 / 1,650 / 1,650 | 1,218 / 464 / 159 | | | +245 / +173 / +49 |
| coord (decimal from D°M'S") | 2,397 | 2,554 | 8.52 | 8.35 | +50 |
| conv (general units) | 185 | 101 | 4.37 | 3.03 | +31 |
| famsize, land_km, hh_copy | | | | | +9 / +3 / +1 |
| **all slots** | 90,012 | 58,350 | | | **+12,972** |

- **The record pays 58.3 KB on these 463 K coded bytes (17% of mid10M's archive); the specialist removes 22% of it.**
- Exact arithmetic (housing density, last-of-sum, area conversion) nearly zeroes the cost; lattices take about 1 bit
  per percentage off on average, much more for small places (race% per slot: P = 128–255 3.70 → 2.00 bits,
  P = 1,024–2,047 5.29 → 4.10, P ≥ 8,192 no gain).
- Surprises: coordinates gain almost nothing (the record already pays only 8.5 bits for a 10-char decimal after
  its D°M'S"), and `hh_copy` is free for the record (match model).
- **Stability:** gain per quarter of mid10M 3,278 / 3,609 / 3,051 / 3,034 B while the record's own in-slot cost
  falls 15.7 → 13.6 KB per quarter: the record warms up on the template, the specialist's information does not decay.
- **General unit conversions** (10M, a non-census sample): 355 slots, record 9.0 bits/slot → 6.2, +126 B.

## 4. Real integration (bench rows) [M]

`patches/exp/E12-arith` (final stage after SSE, `E12_DICT` switch). Every row round-trips byte-exactly.

| slice | record | E12 | Δ |
|---|---:|---:|---:|
| `enwik9_1M` | 126,473 | 126,469 | −4 B (causal sim: −4) |
| `enwik9_mid1M_pa` (10^6 B of enwik9 from offset 500,000,168, census articles) | 31,408 | **30,049** | **−1,359 B (−4.33%)** |
| `enwik9_10M` | 1,264,682 | 1,264,559 | −123 B (causal sim: −127; one census article, mostly unit conversions) |
| `enwik9_mid10M_pa` (10^7 B from offset 500,000,168) | 337,982 | **325,041** | **−12,941 B (−3.83%)** (causal sim on mid10M: −12,928) |

**Mixer lr 0.004** (`E12_LR=0.004`): `enwik9_mid1M_pa` 29,920 B (−129 more than lr 0.002, −4.74% vs the record),
`enwik9_mid10M_pa` **324,627 B (−414 more, −13,355 / −3.95% vs the record)**, both round-trip OK. The offline sim
predicted −429 more on mid10M. lr 0.004 is the default in the C2/C3 combos.

**Full mode:** the -e/archive9 stream codes a 5-byte block header before the WRT flag; the model now skips it
(`PredictorOptions::e12_coded_block_header`). On the real reordered -e stream of full enwik9 (586,459,020 bytes,
from `comp9 -e --bytes-only --save-ppmd-bytes`) it opens exactly the same slots per class as on the identity-order
stream. See `patches/exp/E12-arith/README.md`.

`data/slices/enwik9_mid10M` itself cannot be benched through `slice_cmd.sh` (§7), hence the page-aligned copies in
`build/exp/E12/`. The mid10M simulation (−12.9 KB) and the mid1M_pa bench (−4.3%, the same share as mid10M's 3.8%
with a colder CM) agree. Timing on the 1M census slice: comp 214.6 s vs 201.9 s (+6%, contended box, and the PGO
profile does not exercise the E12 path); per coded byte that is more than the causal sim's 5 µs, so measure again on
an idle box before trusting §6's time estimate.

## 5. Full scale [M counts, ESTIMATE gain]

Slot counts in full enwik9 (`e12_slots.py --counts-only` on `full.pre`): **1,220,909 slots**, 13.6× mid10M
(214,758 race%, 144,152 hh%, 143,479 age%, about 34–36 K of each once-per-article class, 44,657 coordinates,
25,149 conversions). The causal model run over the full WRT stream (`e12_csim ... stream=1`) opens about the same
numbers (248,443 race% and 176,940 age% including false triggers at "with ", 35–37 K of each once-per-article class,
44,675 coordinates, 23,915 conversions), so the triggers generalize across the whole census block. The census population distribution
(log2 P buckets) of mid10M matches full enwik9's closely (both peak at P = 1,024–2,047), so mid10M is representative
of the census articles, unlike its "not representative" label for the rest of the corpus.

**Projection.** Full-scale count of every (class, log2 denominator) cell × the mid10M gain per slot of the same cell
(nearest bucket when a cell has < 20 slots); conversions from the 10M sim.

| assumption | gain |
|---|---:|
| mid10M per-cell rates, no deflation | **190 KB** |
| same, rates divided by 1.3 (E11's warm-CM deflation: full-scale per-byte costs are about 1.3× lower) | 146 KB |
| **central** | **about 170 KB, range 140–190 KB** |

Top contributors: race% 26 KB, housing densities 41 KB, hh% 21 KB, race%last 16 KB, age% 10 KB, hh%cons 10 KB,
conversions 9 KB, sex ratios 14 KB.

Why the deflation may be too pessimistic: the specialist's information (which display values are possible) is not
something more data teaches the record, and the per-quarter gain on mid10M did not shrink while the record's cost did.
Why the central may be optimistic: in the full run articles are reordered (`-e`) and the record has seen 36 K census
articles, so its in-slot costs at the end of the census block are lower than on mid10M. Article order does not change
the specialist (it is per article).

## 6. Cost

- **Time [M, contended box]:** the model only works at trigger points and inside slots. On the full WRT stream
  (586 M coded bytes) the model alone takes **554 s (0.95 µs per coded byte)**; on census-only mid10M 5.1 µs per byte
  (lattice expansion with `snprintf`, cached per article, plus `std::regex` at triggers). Full enwik9 on the
  reference machine [ESTIMATE]: about 0.2 h, against about 7.8 h of headroom.
- **RAM:** one article of text plus one slot's candidates (≤ 200 K strings for the largest towns): under 1 MB typical,
  a few MB peak.
- **S:** +8 KB of uncompressed binary (604,472 → 612,664 B; `std::regex` instantiations are most of it), about
  +3 KB after packaging [ESTIMATE]; a hand-written matcher would shrink it. No archive bytes.

## 7. Tooling finding: `data/slices/enwik9_mid10M` cannot round-trip through slicetool

`slicetool pre` assumes a prefix slice: it treats the first 29 lines as the `<siteinfo>` intro and starts the phda9
transform there. In mid10M that point is inside an article, and `slicetool post` drops its trailing interlanguage
link (`[[io:Dry Ridge, Kentucky]]`, 34 bytes): `pre` then `post` alone gives 9,999,966 bytes, first difference at
byte 3,015. The record's costlog run on mid10M compressed fine (337,980 B, so the cost dump is valid) but failed the
round-trip for this reason, as would any mid10M bench through `slice_cmd.sh`. Workaround used here:
`build/exp/E12/enwik9_mid10M_pa`, 10^7 bytes of enwik9 from offset **500,000,168** (29 lines before the first
`<page>` after 5×10^8), which round-trips through slicetool. Suggest replacing or regenerating the mid slice this way
(or fixing slicetool) in `scripts/fetch_data.sh`.

## 8. What next

1. Full enwik9 integration: handle the `-e` container (each block's WRT flag byte) and verify on 100M.
2. Feed the specialist into the layer-0 mixers as well (not only a final stage), so the CM can learn from it.
3. More derivable classes: county articles' extra tables, the poverty sentence (sample-based, near-lattice), income
   ordering constraints, and dates (age at death from birth and death years).
4. A prior over k (e.g. smooth in value) instead of uniform per lattice point.
5. Mixer lr 0.004 instead of 0.002: offline +13,401 B vs +12,972 on mid10M (eps 1e-5: +13,105; without the direct
   p1 input: +11,949).
