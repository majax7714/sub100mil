# A1: long-range retrieval for the record, design and evidence

**Verdict: NO-GO.** The record already captures long-range repetition. On the 10M slice, every byte that a
long earlier match predicts correctly costs the record about 0.06 bits. Where the record does pay real
bits despite a match, the match is usually wrong. A simulated retrieval mixer on top of the record gains
**−101 to +18 bytes** on 1.26 MB. At full scale, the part of the repetition that lies outside the
record's match windows has a realistic value of about 10 KB, with a loose ceiling of about 0.2 MB. The bits
are in word choice, so the next architectural bet should be transformer-side (§7).

Labels follow docs/refs/fx2-cmix-transformer.md:
- **[M]** measured here.
- **[ESTIMATE]** derived from measurements by an argument stated next to it.
- **UPPER BOUND** is a ceiling, not an expected gain.

Classification of this work: **architectural (evaluation)**. Nothing was added to the compressor.

## 0. Method and reproduction

| Step | What | Where |
|---|---|---|
| Per-byte costs | The record built with the observation-only `FX2_COSTLOG` patch. It writes 40 B per coded byte: the record's cost, transformer/fxcm/PPMD-alone costs, the coder's 8 bit probabilities, and the match models' state | `patches/tools/per-byte-cost/` (README has the format) |
| Run | enwik9_10M in slice mode, via `scripts/slot.sh scripts/bench.sh --no-ledger`, comp + decomp + `cmp` | `build/exp/A1/runs/` (gitignored) |
| Text mapping | Invert the WRT transform of the coded stream back to `slicetool pre` text, keeping a coded byte → text position map. Verified byte-identical to the `slicetool pre` output | `scripts/analysis/a1_analyze.py` |
| Classes | Heuristic content classifier on the phda9 text (§2) | same |
| Matches (slice) | Suffix automaton (exact longest earlier context, and "continued correctly" oracle). Also realistic most-recent-occurrence predictors k = 8/16/32/64, a longest-match follower, and skip-gram "template" predictors. Plus a simulated final-stage mixer that adds them to the record's own bit probabilities | `scripts/analysis/a1_match.cpp` |
| Matches (full) | For all 585,969,895 coded bytes of full enwik9 (identity article order, `slicetool pre` then `cmix -s`): the exact most recent earlier occurrence of each 16/32/64-byte context (sort-based, no hash eviction), its distance, and whether it predicts the next byte | `scripts/analysis/a1_far.cpp`, `a1_far_summary.py` |

```sh
patches/tools/per-byte-cost/build.sh build/exp/A1                       # -> build/exp/A1/cmix_costlog
scripts/slot.sh scripts/bench.sh --no-ledger --method 'A1 costlog 10M' --slice data/slices/enwik9_10M \
  --comp "FX2_COSTLOG=$PWD/cost_10M.bin FX2_BIN=$PWD/build/exp/A1/cmix_costlog patches/fx2-cmix-transformer/slice_cmd.sh comp {in} {out}" \
  --decomp "FX2_BIN=$PWD/build/exp/A1/cmix_costlog patches/fx2-cmix-transformer/slice_cmd.sh decomp {in} {out}"
g++ -O2 -march=x86-64-v3 -o a1_match scripts/analysis/a1_match.cpp; ./a1_match --sam cost_10M.bin m10
build/refs/fx2-cmix-transformer-v1/slicetool pre data/slices/enwik9_10M s10.pre
python3 scripts/analysis/a1_analyze.py --cost cost_10M.bin --pre s10.pre --dict <tree>/dictionary/english.dic --match m10
g++ -O2 -march=x86-64-v3 -o a1_far scripts/analysis/a1_far.cpp; ./a1_far full.stream far.tsv 10000000 16   # ~5 min, 1 GB
python3 scripts/analysis/a1_far_summary.py far.tsv
```

**Instrumented run [M]:**
- 1M: 126,473 B. Identical to the release binary, and the round-trip is OK.
- 10M: archive 1,264,682 B, round-trip OK. This equals the baseline ledger row, so the hook changes nothing.
- The per-byte costs sum to 1,264,639 B. The 43 B difference is the archive header and coder flush.
- Timings were contended (3 slots busy) and are not reported.

## 1. Inventory: the record's match and long-range models

| Model | Where | Count | Context → candidate | Min length / verification | Max length | Output to mixers | Memory |
|---|---|---|---|---|---|---|---|
| cmix `Match` (byte orders) | `predictor.cpp AddMatch` | 5 | `ContextHash` (order, bits) = (0,8), (1,8), (7,4), (11,3), (13,2). The last 3 give 26–33-bit contexts reduced mod 2M. Map: context → last position | **No verification.** It re-looks-up whenever the current match is < 8 *bits* long, otherwise follows | match length counted in **bits**, 0–255 | 1 input each to the 23 layer-0 mixers: a learned P(expected bit) per match length (adaptive, limit 200) | 8 MB map each for the three large ones, plus the shared 60 MB history |
| cmix `Match` (word contexts) | `AddWord` | 5 | `Sparse` over word hashes {0}, {1}, {1,3}, {1,2,3}, {7,2} | as above | as above | 1 input each | 8 MB map each = 40 MB |
| cmix shared history | `ContextManager::history_` | 1 | circular, **60,000,000 bytes** | | | | 60 MB |
| `longest_match_` | | | max over the 10 models of match_length/32 | | | **selector context** of one of the 23 mixers | 0 |
| fxcm MatchModel2 (paq8px v208) | `fxcmv1.cpp` | 1 (up to 4 candidates) | hashes of the last 5, 7 and 9 bytes and of the current word → 2^21 buckets × 4 recent positions | **verified** at registration (5/7/9 bytes). Recovers after a 1-byte mismatch | 62 | 7 inputs to fxcm's mixers (±length, 3 StateMaps × 2). `isMatch` selects fxcm mixer weights | 32 MB table |
| fxcm SparseMatchModel | `fxcmv1.cpp` | 1 | 4 hashes: min length 3, 4, 5, and 6 at stride 2 | verified | 64 | 2 inputs | 4 MB |
| fxcm buffer | `buffer[0x1000000]` | | circular, **16 MiB** | | | | 16 MB |
| PPMD | order 25 | 1 | context tree, effectively the whole file within 14,000 MiB (mmapped) | exact | 25 | 1 input + the transformer's prior | ≤14 GB, mostly on disk |
| Transformer | | 1 | per-article context (SWA W=1024 in 3 layers, KDA over the article piece ≤131,072 tokens). Weights **trained on enwik9** (592 epochs) | | | `byte_mixer_output` to the 23 mixers and layer 1; `lstmpr`/`lstmex` into fxcm | 5.4 MB arenas |

Totals: about 120 MB of cmix match state plus 52 MB of fxcm match state. **No CM match model can reach
further back than 60 MB** (fxcm 16 MiB), but PPMD (order ≤ 25) and the transformer's weights can.

Found during the inventory: `Predictor::Predict` treats input `fxcm_model_index` (the last of fxcm's 431
outputs) as "fxcm's final prediction". It feeds layer 1 and `auxiliary_context_`. But fxcm fills only
**429 of 431** outputs (logged by the tool), so that slot is the constant 0.5. The real fxcm final (output 428)
still reaches the 23 layer-0 mixers, so the loss is probably small. It is a free tuning item, though (§7).

## 2. Where the bits go: 10M slice [M]

Coded stream: 5,764,509 bytes (WRT) → 9,485,082 bytes of `slicetool pre` text. The record pays 1,264,639 B
(1.755 bits per coded byte).

**Classifier (heuristics on the phda9 text):**
- phda9 notation: `[x]` is an internal link (`[[x]]` in the original), `[[x]]` is an external link,
  `{x}` is a template, and `{{| … |}}` is a table.
- Painting order (later wins): prose < HTML tag/entity < heading < link anchor < link target <
  `[Image:…]` < external link/URL < `[Category:…]` < template < table < infobox (template name `*box`) <
  `<ref>` < `<math>`.
- Whole-page and whole-section overrides: `#REDIRECT` pages; page XML outside `<text>` (tags, title,
  comment); and the slice.pre sections phda9 moves out of the articles (the metadata block of
  ids/timestamps/users, the interlanguage-link block, intro/coda).
- A coded byte takes the class of the first character it produces. A dictionary word code takes the class
  of its word.

| class | text bytes | coded bytes | record bytes | share | bits/text byte | bits/coded byte | TF-alone / record |
|---|---:|---:|---:|---:|---:|---:|---:|
| prose | 5,803,394 | 3,226,762 | 806,498 | **63.8%** | 1.112 | 2.000 | 1.00 |
| link target | 1,142,274 | 767,770 | 180,815 | **14.3%** | 1.266 | 1.884 | 1.01 |
| ref/citation | 580,978 | 353,981 | 76,602 | 6.1% | 1.055 | 1.731 | 1.01 |
| external link/URL | 336,154 | 224,108 | 51,819 | 4.1% | 1.233 | 1.850 | 1.01 |
| interlanguage block | 284,724 | 326,148 | 27,923 | 2.2% | 0.785 | 0.685 | 1.05 |
| table | 213,158 | 162,321 | 21,391 | 1.7% | 0.803 | 1.054 | 1.02 |
| image/file | 117,582 | 73,544 | 17,279 | 1.4% | 1.176 | 1.880 | 1.01 |
| template | 140,063 | 96,738 | 15,477 | 1.2% | 0.884 | 1.280 | 1.03 |
| link anchor | 154,175 | 94,637 | 11,144 | 0.9% | 0.578 | 0.942 | 1.01 |
| heading | 106,629 | 64,553 | 10,941 | 0.9% | 0.821 | 1.356 | 1.01 |
| metadata block (ids/time/user) | 131,684 | 81,829 | 10,260 | 0.8% | 0.623 | 1.003 | 1.01 |
| infobox | 87,926 | 61,149 | 8,863 | 0.7% | 0.806 | 1.160 | 1.02 |
| page XML (tags) | 124,135 | 78,086 | 7,359 | 0.6% | 0.474 | 0.754 | 1.01 |
| edit comment | 31,526 | 17,828 | 4,270 | 0.3% | 1.084 | 1.916 | 1.06 |
| category | 85,176 | 43,331 | 4,156 | 0.3% | 0.390 | 0.767 | 1.02 |
| html/entity | 94,127 | 58,109 | 3,479 | 0.3% | 0.296 | 0.479 | 1.01 |
| title | 19,047 | 10,596 | 3,365 | 0.3% | 1.413 | 2.541 | 1.12 |
| math | 18,024 | 14,624 | 2,171 | 0.2% | 0.963 | 1.187 | 1.01 |
| redirect page, header/coda | 14,306 | 8,395 | 827 | 0.1% | | | |

**Lexical overlay, all classes:**

| type | text bytes | share of bits | bits/text byte |
|---|---:|---:|---:|
| ASCII letters | 6,896,872 | 81.2% | 1.192 |
| digits (numbers, dates) | 229,214 | 4.5% | 1.995 |
| non-ASCII (UTF-8, non-English) | 88,599 | 1.3% | 1.512 |
| space, punctuation | 2,270,397 | 12.9% | 0.576 |

**How concentrated the bits are:**
- **Bytes the record codes at ≥4 bits are 17.5% of coded bytes and carry 61.2% of the bits.** Bytes under
  0.1 bit are 39.3% of the bytes and carry 0.4% of the bits.
- WRT word-code bytes (≥0x80) carry 67.5% of the bits at 2.9 bits each. These are the choice of the next
  word.
- The transformer alone (its 205-way distribution, bit-decomposed) costs 1,275,720 B. That is only 0.9% more
  than the record. The ratio is 1.00–1.03 in every big class.
- fxcm alone costs 1,277,500 B, but fxcm already takes the transformer's prediction as an input.

**Slice caveats (docs/EXPERIMENTS.md):**
- The CM is cold.
- The slice keeps original article order, so there is no census region.
- Every slice is inside the transformer's training data. The full file is too.
- neif's full-file trace puts about 90% of the payload in ordinary article text (LANDSCAPE §3.1), consistent
  with prose, links and refs being 84% here.

## 3. Retrieval headroom on the slice [M]

All predictors are causal: the history is the already-coded part of the slice.

**How to read the table:**
- "right" = the predicted byte equals the coded byte.
- "record bytes on right" = what the record paid on exactly those bytes, split by the record's own cost of
  the byte.

| predictor | coverage | accuracy | right / all bytes | record bytes on right | of total | rec <0.1 b | 0.1–1 b | 1–4 b | ≥4 b |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| oracle: some earlier 8-byte context continued correctly | 26.9% | 77.0% | 20.7% | 63,689 | 5.04% | 1,401 | 9,570 | 33,610 | 19,107 |
| oracle: … 16-byte | 6.8% | 89.7% | 6.1% | 8,315 | 0.66% | 376 | 1,551 | 4,103 | 2,285 |
| oracle: … 32-byte | 2.0% | 93.5% | 1.8% | 2,052 | 0.16% | 106 | 293 | 793 | 860 |
| oracle: … 64-byte | 0.4% | 96.9% | 0.4% | 132 | 0.01% | 23 | 33 | 54 | 23 |
| most recent k=8 | 26.0% | 67.4% | 17.5% | 24,071 | 1.90% | 1,261 | 7,039 | 13,023 | 2,747 |
| most recent k=16 | 6.6% | 85.6% | 5.6% | 3,713 | 0.29% | 356 | 1,276 | 1,797 | 284 |
| most recent k=32 | 1.9% | 90.4% | 1.7% | 759 | 0.06% | 101 | 248 | 346 | 64 |
| longest-match follower, len ≥16 | 6.8% | 85.5% | 5.8% | 3,760 | 0.30% | 367 | 1,322 | 1,802 | 269 |
| longest-match follower, len ≥32 | 2.0% | 90.1% | 1.8% | 759 | 0.06% | 104 | 255 | 327 | 73 |
| skip-gram (digits/names wildcarded) right where every exact predictor is wrong or absent | 51.1% | 11.4% | 5.8% | 14,985 | 1.18% | 428 | 2,846 | 7,815 | 3,896 |

**Does the record already have it?** It does.

1. **Long matches are nearly free.**
   - Bytes that a ≥32-byte match predicts correctly cost the record **0.059 bits** each, 759 B in total.
     For ≥16-byte matches the figure is 0.090 bits.
   - For 99.3% of the ≥32 bytes, the record's own match models (fxcm MatchModel2 or a cmix `Match` ≥16 bits)
     predict the same byte.
   - On the other 0.7% (758 bytes) the record pays 1.06 bits each: 100 B in total.
2. **The transformer alone has memorized the far repeats.** On ≥32-match-right bytes, by distance to the
   earlier copy:

   | distance | bytes | record bits/byte | transformer alone | fxcm alone |
   |---|---:|---:|---:|---:|
   | <1K (inside the transformer's attention window) | 33,238 | 0.055 | 0.094 | 0.059 |
   | 1K–10K | 20,942 | 0.058 | 0.091 | 0.064 |
   | 10K–100K | 17,935 | 0.059 | 0.079 | 0.064 |
   | 100K–1M | 17,450 | 0.066 | 0.079 | 0.069 |
   | ≥1M (always another article) | 12,928 | 0.063 | **0.073** | 0.068 |

   Without any CM, the transformer pays 0.073 bits on repeats from other articles a megabyte back.
   Its per-article context can't see them, so this is memorization.
3. **Where the record pays despite a match, the match is usually wrong.** Accuracy of the ≥16 longest-match
   predictor, conditioned on the record's cost of the byte:

   | record cost | <0.1 b | 0.1–1 b | 1–4 b | ≥4 b |
   |---|---:|---:|---:|---:|
   | bytes with a ≥16 match | 301,160 | 41,173 | 29,612 | 17,412 |
   | match accuracy | 97.4% | 76.4% | **26.0%** | **2.4%** |

   The record knows when to distrust the match.
   - The "opportunity bucket" (match right, record ≥1 bit) is **2,071 B (0.16%)** for ≥16 matches and
     400 B for ≥32 matches.
   - Recovering it would require telling these bytes apart from the 74–98% of same-looking cases where the
     match is wrong.
4. **Simulated retrieval mixer.**
   - Setup: an online logistic mixer per bit. Its inputs are stretch(p_record), a bias, and 7 retrieval
     predictors (k8/16/32/64, longest-match, skip16/32). Each predictor carries an adaptive per-(length,
     bit position, confidence) reliability. Weights are selected by match length × record confidence.
   - It is compared with the same mixer without the retrieval inputs, which isolates the information they add.
   - **Gain: −101 B at lr 0.002, +1 B at 0.0006, +18 B at 0.0002, −440 B at 0.006** on 1,264,639 B.
     That is zero.
5. **Skip-gram ("template") matches add nothing usable.** Where every exact predictor fails, the
   wildcarded context predicts right 11% of the time.
6. **History growth inside the slice.**
   - The first half has 1.5% of bytes ≥32-match-right; the second half has 2.0%.
   - The opportunity bucket goes from 742 B to 1,329 B.
   - Repetition grows with history, but it is still tiny in absolute terms.

## 4. Full scale: 586 MB coded stream, identity article order [M]

| k | context seen before | most recent copy predicts right | accuracy | right, source <1M | 1M–16M | 16M–60M | 60M–200M | >200M |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 16 | 25.55% | 22.12% | 0.866 | 15.90% | 2.80% | 1.40% | 1.28% | 0.74% |
| 32 | 13.30% | 12.15% | 0.914 | 8.34% | 2.14% | 0.81% | 0.57% | 0.30% |
| 64 | 4.93% | 4.64% | 0.942 | 2.64% | 1.14% | 0.49% | 0.25% | 0.13% |

By stream offset (k=32, share of each window's coded bytes):

| offset | right | right, source ≥16M (outside fxcm) | right, source ≥60M (outside every CM match model) |
|---|---:|---:|---:|
| 0–50M | 2.2% | 0.16% | 0.00% |
| 100–150M | 4.9% | 1.46% | 0.75% |
| 200–250M | 11.4% | 1.51% | 0.80% |
| 250–300M (census) | 44.4% | 1.87% | 0.38% |
| 300–350M (census) | 32.5% | 3.92% | 0.95% |
| 450–500M | 10.9% | 2.93% | 2.35% |
| 550–586M | 8.9% | 0.46% | 0.36% |

- Full-file repetition is several times the slice's: 12.2% of bytes are ≥32-match-right vs 1.8%. Census
  pages are the extreme, but non-census windows are still 5–11%.
- Most sources are near: 69% under 1 MB, 86% under 16 MiB. That is inside the record's windows.
- Only **0.87% of coded bytes (k=32) and 2.02% (k=16)** are predicted by a copy more than 60 MB back.
- Caveat: these numbers use identity order. The real `-e` run reorders articles to put similar ones together,
  which shortens distances, so the far fractions here are if anything high.

## 5. Upper bounds at full scale (ESTIMATES)

Each bound converts byte counts from §4 into bits using slice costs from §3.

| bucket | bytes at full scale | bits per byte used | UPPER BOUND | realistic |
|---|---:|---|---:|---:|
| A. repeats from **>60 MB** back (k≥16): no CM match model can reach them | 11.8M (2.02%) | transformer-alone cost on ≥1M-distance match-right bytes: 0.111 b. Assumes the record pays all of it and a perfect retriever pays 0 | **164 KB** | the record already pays only 0.103 b on such bytes in the slice, where its CM could see them. The difference, 0.008 b, is worth **≈12 KB** (k≥32: ≈6 KB) |
| B. "pays a lot although a match is available" (k≥16, record ≥1 bit) | 0.0066 record bytes per k16-right byte (slice second half: 1,329 B over 200K bytes) × 129.6M k16-right bytes | all record bits in the bucket | **≈0.86 MB** (≈0.78 MB above an ideal 0.2-bit match cost). Loose: census bytes, which dominate the full-file k16 count, are much cheaper than slice bytes | match accuracy in this bucket is 2–26% (§3.3), and the slice simulation recovers 0 of the 2.07 KB. Realistic **≈0** |
| A+B | | | **≈1.0 MB** (cannot all be had) | **≈10–20 KB** |

**The "realistic" column is a judgement from the slice evidence, not a measurement at full scale.** No full
encode was run, because the evidence doesn't justify about 60 h of CPU.

## 6. Candidate designs, with costs

Budget (docs/refs/fx2-cmix-transformer.md §7): **≈0.3 GB RAM** and **≈7.8 h ≈ 48 µs per coded byte** of headroom.

**R1. Whole-file long-match model (disk-backed).** Targets bucket A.
- The coded history is appended to a scratch file (586 MB on disk, allowed; precedent: PPMD's mmap).
- Index: content-defined anchors, i.e. 32-byte contexts whose hash ≡ 0 mod 16 (~37M anchors), stored as
  2^25 × 4 B most-recent positions = **128 MB**.
- Lookup: only at anchors and only when no match is active. A `pread` of 256 B verifies and then follows the
  candidate.
- Output: 2 inputs to the 23 layer-0 mixers (expected bit × learned length confidence) plus a mixer
  selector.
- Time: rolling hash ~5 ns/byte, one DRAM-miss lookup per 16 bytes, a pread on about 1% of bytes, and 2×23
  extra MACs. Total ≈0.3 µs per coded byte ≈ **0.05 h** full run.
- RAM: 128 MB, plus page cache, which isn't RSS but should be checked against the rules.
- Expected gain: **≈12 KB** (ceiling 164 KB).
- Risk: disk I/O in the decompressor, and a second copy of the history.

**R2. Template / skip-gram match model** inside the existing 60 MB history. Targets infobox, table and
census boilerplate with varying fields.
- Key: the last 16–32 normalized tokens (digit runs and capitalized words collapsed) plus the offset inside
  the current field.
- Size: 2^22 × 4 B = 16 MB. Time <0.5 µs per byte.
- Expected gain: **≈0**. Its exact-miss accuracy is 11% on the slice. Census pages might be the exception,
  but the record already codes them at about 0.4 bits per byte (LANDSCAPE §3.1).

**R3. Match-aware final APM** (cheapest). An SSE stage after `mixer_1_`, keyed by (fxcm MatchModel2 length
bucket, expected bit, bit position).
- Cost: 0 RAM, <0.1 µs.
- Expected gain: ≈0 B. This is exactly what the §3.4 simulation measured.

None of the three justifies implementation time ahead of the alternatives in §7.

## 7. Recommendation

**NO-GO on A1 as specified.** Retrieval over the file itself is already done by the record. In-window CM
match models make long repeats cost 0.06 bits, and outside the window the memorized transformer
does nearly as well. The plausible full-scale gain is ~10–20 KB against a ~5 MB target.
**Keep the per-byte tool**: it is the instrument for the alternatives below.

**What the per-byte data points to instead:**

1. **Word choice in article text is the whole game.**
   - Prose, link targets and refs carry 84% of the bits.
   - 61% of all bits sit on the 17.5% of bytes the record codes at ≥4 bits, mostly WRT word codes at
     2.9 bits each.
   - On those bytes the transformer alone is *as good as* the full record (TF/record = 0.99), so the CM
     ensemble adds almost nothing where the bits are.
   - Levers that move the transformer's word distribution:
     - E2 (online correction head / fast weights; in progress);
     - LANDSCAPE D1 (context injection: put retrieved *related text* into the transformer's context, as
       opposed to byte-level copy prediction, which this study rules out);
     - D4 (dynamic evaluation).
2. **Link targets are the costliest structured class: 1.27 bits per char vs 1.11 for prose, 14.3% of bits.**
   - Split by whether the target was seen before in the slice [M]:
     - never-seen targets: 46,810 links, **1.61 bits/char, 10.9% of all bits**;
     - targets seen earlier as a link: 43,850 links, 0.73 bits/char, 3.2%;
     - targets seen as a title: 1,470 links, 0.59 bits/char.
   - At full scale far more targets have been seen before, since all 243K titles appear somewhere.
   - A *set-retrieval* model is the useful form of "retrieval" here:
     - inside `[`, rank the earlier titles and link targets that extend the current prefix, by frequency
       and recency;
     - give the transformer/mixer the top candidate's next byte and its margin.
   - It is cheap to test with this tool: −log2 rank-model cost on link bytes vs the record's cost, offline.
     Do that before building anything.
3. **Free tuning item:** feed fxcm's real final output (index 428, not the constant slot 430) to layer 1 and
   `auxiliary_context_` (§1). It costs one line and a 10M run, and needs a fresh baseline because the output
   changes. Classify it as tuning.
4. **Numbers and dates** are 4.5% of the bits at 2.0 bits per digit. A digit-arithmetic or date model
   (LANDSCAPE D5) is a bounded side bet.
