# A2: link-target model for the record, design and evidence

**Verdict: NO-GO.** A model that predicts `[[...]]` targets by ranking earlier link targets and titles that
extend the typed prefix adds almost nothing on top of the record. In a per-bit mixer simulation it gains **208 B on
the 10M slice** (0.11% of link bits, 0.016% of the total) and **16 B on 1M**. The record already codes a
previously seen target at **7.6 bits per link**. A frequency-ranked candidate set needs **13.1 bits** for the same
links, *even when it is told for free that the target is in the set*. The only part with a positive effect is
recall of targets **already linked in the same article**. It projects to roughly **0–40 KB on full enwik9
(central estimate about 17 KB)**, well under the 100 KB bar.

Labels follow docs/design/A1-retrieval.md:
- **[M]** measured here.
- **[ESTIMATE]** derived from measurements by an argument stated next to it.
- **UPPER BOUND** is a ceiling, not an expected gain.

Classification of this work: **architectural (evaluation of a specialist model)**. Nothing was added to the compressor,
and there is no ledger row because no archive was produced.

## 0. Method and reproduction

| Step | What | Where |
|---|---|---|
| Per-byte costs | The record's cost dumps from A1 (`FX2_COSTLOG`, 40 B per coded byte, including the coder's 8 per-bit probabilities). No new compressor runs | `build/exp/A1/runs/cost_{1M,10M}.bin` (gitignored) |
| Spans | WRT-decode the coded stream (A1's `wrt_decode`, verified byte-exact against `slicetool pre`). Then cut every internal link target (after `[`, through its terminator `\|` or `]`), every piped link's anchor (through `]`) and every `<title>` into **coded-stream** ranges. Category/Image/File/Media links are excluded, as in A1 | `scripts/analysis/a2_extract.py` |
| Model + mixing simulation | Prefix tries over coded byte sequences, plus per-bit and byte-level mixing with the record (§1) | `scripts/analysis/a2_sim.cpp` |
| Full-scale counts | Text-level link parse of all 243,425 pages of full enwik9 (identity-order `slicetool pre`). The same pass is replayed in the record's `-e` article order (`new_article_order` + `article_reorder.h`'s remap, redirect pages appended last) | `scripts/analysis/a2_fullscale.py` |

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic
python3 scripts/analysis/a2_extract.py --cost build/exp/A1/runs/cost_10M.bin --pre build/exp/E2/proxy/10M.pre \
  --dict $D --out build/exp/A2/spans_10M.tsv                                         # 2 s
g++ -O2 -march=x86-64-v3 -o build/exp/A2/a2_sim scripts/analysis/a2_sim.cpp
build/exp/A2/a2_sim build/exp/A1/runs/cost_10M.bin build/exp/A2/spans_10M.tsv sel=2 lr=0.001   # 2 s
python3 scripts/analysis/a2_fullscale.py build/exp/A1/full/full.pre \
  third_party/fx2-cmix-transformer-v1/src/readalike_prepr/data/new_article_order --slice-pages 1325  # 66 s, 1.6 GB
```

Slice spans [M]:
- 10M: 91,631 link targets (796,819 coded bytes after `[`, 186.0 KB of record cost), 15,691 piped anchors and 1,325 titles.
- 1M: 7,030 links (63,606 coded bytes, 14.9 KB).
- The A2 seen/never-seen split agrees with A1's: 51% of links never seen before in the slice.

## 1. The model

Everything is causal: a sequence enters an index only after its last byte has been coded. The experts work on
the **coded (WRT) bytes**, so a dictionary word inside a target is one to three symbols. That is the alphabet the
coder actually codes, and it avoids mapping characters to codes. At each coded byte of a link target, each expert
returns counts of the next coded byte over the candidates that extend the typed prefix:

| expert | candidates |
|---|---|
| LINK | all earlier link targets, frequency counts (the actual terminator `]`/`\|` included) |
| LINKR | the same with recency-decayed counts (τ = 2,000 links; τ from 200 to 20,000 made no difference) |
| TITLE | earlier `<title>`s. A virtual end symbol is split into `]`/`\|` by global counts |
| ART | targets linked earlier **in the same article** |
| FOLD | links and titles with the leading capital flag (decoded 0x40) removed, i.e. first-letter case folding |
| anchors | after `\|`: the link's own target (folded), earlier anchors of the same target, and earlier anchors of any target |

**Per-bit simulation (the realistic form).** The cost dump has the coder's P(bit=1) on the actual byte's path,
so the simulation can mix bit by bit, as the record's mixers would:
- Each expert's mass on the two halves of the remaining byte range becomes (n0, n1).
- An adaptive StateMap keyed by (expert, q(n0), q(n1), typed-length bucket) turns that into a probability.
- A logistic mixer combines stretch(p_record), a bias and the expert outputs. Its weight set is selected by typed
  length × bit position × "LINK has an opinion" × record confidence (3 levels) × "ART has an opinion".
- The baseline is the **same mixer with only stretch(p_record) and the bias**, so the comparison isolates what
  the experts add. Re-calibration alone gains 68 B on 10M; that is not credited to A2.

**Byte-level cross-check.** p = λ·p_rec(byte) + (1−λ)·q(byte), where q comes from the LINK+TITLE counts and λ is
learned online per context (typed length × candidate count × top-candidate share).

## 2. Results on the slices [M]

Configuration: all experts, lr = 0.001 (the best of a sweep from 0.0005 to 0.008; lr ≥ 0.004 loses).

| slice | link-target bits (record) | A2 gain, per-bit sim | of link bits | of total | byte-level linear mix | anchors (piped text) |
|---|---:|---:|---:|---:|---:|---:|
| 1M | 14,903 B | **+16 B** | 0.10% | 0.013% | −76 B | 0 B of 1,530 |
| 10M | 185,972 B | **+204 B** | 0.11% | 0.016% | −398 B | +4 B of 13,120 |

10M split (per-bit sim):

| subset | links | record B | A2 gain B | % of subset |
|---|---:|---:|---:|---:|
| never seen before (as link or title) | 46,824 | 143,602 | **−143** | −0.10% |
| seen before as a link | 44,674 | 42,183 | **+346** | +0.82% |
| … of which earlier **in the same article** | 14,361 | 11,718 | **+438** | +3.73% |
| … of which only in **other** articles | 30,313 | 30,465 | −91 | −0.30% |
| seen before only as a title | 133 | 187 | 0 | 0.0% |
| first coded byte of the target | 91,631 | 14,855 | +103 | +0.69% |
| seen-before links, first half of the slice | 19,713 | 18,303 | +189 | +1.04% |
| seen-before links, second half | 24,961 | 23,880 | +157 | +0.66% |

Other configurations on 10M:
- Same-article expert alone: +165 B (+391 on same-article links, −126 on other seen links, −100 on never-seen).
- LINK alone: +98 B.
- No configuration exceeded +219 B.
- The 1M slice shows the same pattern: +19 B on same-article links and about 0 elsewhere.

**Why it doesn't work: the record is already a far better ranker than a candidate set.** Per-link record cost
on 10M, and two oracle bounds:

| | bits per link |
|---|---:|
| record, target never seen before | 24.5 |
| record, target seen before (anywhere) | **7.6** |
| frequency set model on the same seen links, **told for free** that the target is in the set | **13.1** |
| record, target seen earlier in the same article | 6.5 |
| same-article set model, told for free | 6.4 |

- **UPPER BOUND (oracle per-link switch, 10M):** min(record, set model) per link saves 1,544 B with the global set
  and 3,196 B with the same-article set. That is 0.8% and 1.7% of link bits. The oracle knows which links were seen
  and which source wins; a real model pays for both. The simulation recovers about 14% of the same-article bound.
- The transformer is trained on all of enwik9 and sees the article's context. In effect it already knows which
  target fits *here*, which a prefix-frequency ranking can't know. Where the set model does disagree with it, it is
  mostly wrong, which is why links that are never seen or seen only in other articles come out negative.
- Per-link gain on seen links falls from the first half of the slice to the second (+1.04% → +0.66%). More history
  doesn't help.
- Anchors: the text after `|` almost never equals the target (163 of 15,691; unpiped links already cover that case).
  The record pays only 0.92 bits per coded anchor byte, and the anchor experts gain 4 B.

## 3. Full scale [M counts, ESTIMATE gain]

Link statistics of full enwik9 (8,652,868 links, 127.5 MB of target text). The slice calibration (first 1,325 pages)
reproduces the simulator's split: 50.9% never seen vs 51.1%, and 15.8% same-article vs 15.7%.

| bucket | 10M slice links | full, identity order | full, record's `-e` order |
|---|---:|---:|---:|
| never seen | 46,867 (50.9%) | 1,468,225 (17.0%) | 1,455,647 (16.8%) |
| seen earlier in the same article | 14,551 (15.8%) | 1,260,411 (14.6%) | 1,260,411 (14.6%) |
| seen only in other articles | 30,580 (33.2%) | 5,911,279 (68.3%) | 5,911,279 (68.3%) |
| … of which seen ≥100 times | 404 | 2,341,569 | 2,323,372 |
| seen only as an earlier title | 126 (0.1%) | 12,953 (0.1%) | 25,531 (0.3%) |
| never seen as a link, but the title of one of the previous 1,000 pages | 124 | 2,648 | 18,173 |

- **What reordering changes:** almost nothing for links. The reorder groups similar articles, so twice as many
  targets are an earlier title, often a page shortly before. That's still only 0.3% of links, and redirect pages
  (71,110, the alternative names) all go to the end of the order.
- **What scale changes:** "never seen" shrinks from 51% to 17% of links, and "seen only in other articles" grows to 68%.
  This is the bucket where the slice simulation is ≤ 0. In it, frequent targets (seen ≥100 times) cost the record
  very little. The same-article bucket stays about 15% of links, since its share doesn't depend on history.

**Projection [ESTIMATE].** Multiply the 10M per-link gain of each bucket by the full-scale link count of that
bucket. Per-link gain is used rather than per-byte gain because the target length distribution shifts at full scale.

| bucket | 10M gain per link | full-scale links | projected |
|---|---:|---:|---:|
| same article | +0.0305 B | 1,260,411 | +38 KB |
| other articles only | −0.0030 B | 5,911,279 | −18 KB |
| never seen | −0.0031 B | 1,455,647 | −4 KB |
| earlier title only | 0 | 25,531 | 0 |
| anchors | +0.0003 B | about 1.5M | +0.4 KB |
| **total** | | | **about +17 KB (range 0 to +40 KB)** |

**Which way the bias goes:**
- **Upward:**
  - A real implementation feeds the input to the record's 23 layer-0 mixers and final SSE. Those have far richer
    selector contexts than this 5-way-selected final mixer, so they should gate away most of the losses on
    never-seen and other-article links. If every loss were removed, the result would be the +40 KB end of the range.
  - At full scale the mixer has about 90× more training links.
- **Downward:**
  - The simulation mixes at the very end, after the record's final SSE. That is the most favorable place for a
    residual model; inside the network some of the signal is diluted.
  - At full scale the CM is warm, so same-article repeats inside its windows are cheaper for the record too.
  - The same-article gain itself is only 14% of its oracle bound.
- **Overall:** realistically **10–30 KB**, below the 100 KB bar by a factor of 3–10. The loose ceiling is the
  same-article oracle scaled to full size: 3,196 B / 14,385 links × 1.26M links ≈ **280 KB (UPPER BOUND)**. It isn't
  reachable, because it assumes per-link oracle switching.

## 4. Runtime and RAM of a real implementation [ESTIMATE]

| design | RAM | time on the reference machine | value |
|---|---|---|---|
| **Same-article only** (ART): per-article trie of link targets, reset at each `<page>` | < 1 MB (the 10M slice's largest article has 6.2K coded link bytes, about 100 KB of trie; even a 10^5-byte list article would need under 2 MB) | Active only inside link targets, about 14% of coded bytes (≈ 80M at full scale). One trie step per byte, one StateMap lookup per bit and one extra mixer input: < 0.1 µs per link byte, **< 0.05 h** | the only positive part: +34–38 KB if its losses are gated away, about +7 KB if not |
| Global (LINK/TITLE/FOLD) | 1.48M distinct targets + 243K titles ≈ 18M coded bytes of distinct sequences, so about 18M trie edges at 8–12 B ≈ **150–220 MB**. A hashed prefix table (2^24 × 8 B = 128 MB) is the compact alternative. This barely fits the 0.3 GB headroom | similar to ART, plus one DRAM miss per link byte (the CM side is already DRAM-latency bound): ≈ 0.1 µs × 80M ≈ **0.01–0.05 h** | ≤ 0 on the slice |

Both fit the time budget. The global index costs about half the remaining RAM headroom for no measured gain, so
only the same-article piece would be worth building, and it's worth ≤ 40 KB.

## 5. Recommendation

**NO-GO on A2 as specified.** Attic: `attic/2026-11-05-A2-link-targets/`.

What the data points to instead:

1. **In-article recall is the one positive signal, and it isn't specific to links.**
   - The record pays 6.5 bits for a link target that already appeared in the same article. The transformer's
     per-article context (SWA 1024 + KDA) evidently doesn't recall exactly.
   - The general version is an **in-article cache model**: a recency-weighted distribution over WRT word codes (and
     multi-word phrases) used earlier in the current article, fed to the mixers. That is the classic cache LM.
   - It targets prose (64% of bits) as well as links. It can be tested offline in an afternoon with the same tools:
     reuse `a2_sim.cpp`'s BitSim over all word-code bytes, with the experts keyed by article.
   - This is the best follow-up.
2. **Fold the same-article link expert into that cache model** rather than building it on its own. On its own it is
   worth ≤ 40 KB (≈ 0.04%), comparable to E1.
3. Titles and link targets as **context for the transformer** (LANDSCAPE D1, neif's link-context rows) remain
   the way to use link structure. Ranking by prefix is not.
