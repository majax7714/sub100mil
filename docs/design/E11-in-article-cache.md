# E11: in-article cache model for the record, design and evidence

**Verdict: NO-GO (closest miss so far).** A cache over the WRT tokens the current article has already used
(unigram, recency, bigram, trigram and rarity-weighted experts, reset at every `<page>`) adds **+702 B on the 10M
slice (0.056%)** and +30 B on 1M in a per-bit mixer simulation. Projected to full enwik9, it is worth **about 60 KB
(range 45–90 KB) [ESTIMATE]**, below the 100 KB bar. Cost would be acceptable (about 0.2 h, under 50 MB). The gain
on in-article repeats of rare tokens is real (+2.1 KB on 10M), but it is eaten by losses on tokens the article uses
for the first time (−1.4 KB). The record already recalls in-article tokens better than any cache: 5.8 bits for a
token used once before in the article, vs 8.8 bits for the cache *told for free* that the token is in it.
E10 (dynamic evaluation) targets the same in-article signal, so the two would overlap.

Labels as in docs/design/A1-retrieval.md:
- **[M]** measured here.
- **[ESTIMATE]** derived from measurements by an argument stated next to it.
- **UPPER BOUND** is a ceiling, not an expected gain.

Classification: **model addition (evaluation of a non-transformer specialist)**. Nothing was added to the
compressor, and there is no ledger row because no archive was produced.

## 0. Method and reproduction

| Step | What | Where |
|---|---|---|
| Per-byte costs | The record's cost dumps from A1 (`FX2_COSTLOG`, including the coder's 8 per-bit probabilities). No new compressor runs | `build/exp/A1/runs/cost_{1M,10M}.bin` |
| Per-byte labels | WRT-decode (A1's verified decoder), A1 content class and article id (`<page>` index; the trailing metadata, interlanguage and XML sections are separate groups) for every coded byte | `scripts/analysis/e11_extract.py` |
| Model + mixing simulation | Token caches over coded bytes, per-bit StateMaps and a logistic mixer against a record-only baseline (§1) | `scripts/analysis/e11_sim.cpp` |
| Global df labels | Text hash of every coded token (`e11_tokhash.py`), its document frequency in full enwik9 (`e11_fullscale.cpp`) | `scripts/analysis/e11_tokhash.py`, `e11_fullscale.cpp` |
| Full-scale counts | Text-level tokens of all 243,425 pages: in-article repeat × df tables, identity and `-e` order (order dump via A2's `reorder()`) | `scripts/analysis/e11_fullscale.cpp`, `e11_order.py` |

```sh
D=build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic; E=build/exp/E11
python3 scripts/analysis/e11_extract.py --cost build/exp/A1/runs/cost_10M.bin --pre build/exp/E2/proxy/10M.pre \
  --dict $D --out $E/aux_10M.bin                                                      # 1 min
python3 scripts/analysis/e11_tokhash.py --cost build/exp/A1/runs/cost_10M.bin --pre build/exp/E2/proxy/10M.pre \
  --dict $D --out $E/tokhash_10M.bin
g++ -O2 -march=x86-64-v3 -o $E/e11_fullscale scripts/analysis/e11_fullscale.cpp
$E/e11_fullscale build/exp/A1/full/full.pre tokhash=$E/tokhash_10M.bin gdfout=$E/gdf_10M.bin   # 35 s, 2.4 GB
g++ -O2 -march=x86-64-v3 -o $E/e11_sim scripts/analysis/e11_sim.cpp
E11_GDF=$E/gdf_10M.bin $E/e11_sim build/exp/A1/runs/cost_10M.bin $E/aux_10M.bin experts=31 lr=0.0005 nobound=1  # 20 s, 320 MB
python3 scripts/analysis/e11_order.py build/exp/A1/full/full.pre \
  third_party/fx2-cmix-transformer-v1/src/readalike_prepr/data/new_article_order $E/order_e.txt
$E/e11_fullscale build/exp/A1/full/full.pre $E/order_e.txt                              # history-df tables, both orders
```

## 1. The model

**Tokens** live in the coded stream. A token is a maximal run of word bytes: logical a–z, A–Z, 0–9, dictionary-code
bytes ≥ 0x80, the case flags 0x40/0x07/0x06, and escape 0x0C with its byte. A dictionary word is therefore one to
three code bytes, and numbers and dates are tokens too. A cache stores a token's raw coded bytes **plus its
terminator** (the separator that ended it), so it also predicts where the token stops. Everything is causal: a
token enters the caches only after its terminator is coded.

**Experts** (prefix tries keyed by a context hash; each returns counts of the next coded byte among the cached
tokens that extend the typed prefix):

| # | name | content |
|---|---|---|
| 0 | AU | in-article unigram, frequency counts |
| 1 | AUR | in-article unigram, recency-decayed (τ = 100 tokens; 30 and 1,000 were worse) |
| 2 | AB1 | in-article bigram: tokens that followed the previous token earlier in this article |
| 3 | AB2 | in-article trigram |
| 4 | ARU | in-article unigram over rare tokens: those in ≤ 2 earlier articles (or, `ralpha`, weighted 1/(1+df)^α) |
| 5–7 | GUR, GB1, GB2 | the same unigram (τ = 5,000), bigram and trigram over the **whole file**, never reset (the contrast) |

**Per-bit simulation (A2's method).**
- For each bit, each expert's mass (n0, n1) on the two halves of the remaining byte range goes through an adaptive
  StateMap keyed by (expert, q(n0), q(n1), position bucket).
- A logistic mixer combines stretch(p_record), a bias and the expert outputs. Its weight set is selected by
  position bucket × bit × record confidence (3) × any in-article expert has an opinion × any whole-file expert has an opinion.
- The **baseline is the same mixer with only stretch(p_record) and the bias**, using the same selection.
  Recalibration alone gains 50 B on 10M; that is not credited to E11.
- Position buckets are: token boundary after a space, after other, and typed 1 / 2 / 3–5 / 6+ bytes of the token.
  `nobound=1` silences the experts at boundaries, where they only lose.

**Analysis labels** (accounting only, never used to predict) for each token:
- its in-article repeat count (0, 1, 2–4, 5+);
- the number of *earlier* articles of the slice that contain it (history df);
- the number of pages of **full enwik9** that contain it (global df, gdf). gdf is a property of the token,
  independent of history and article order, so it is what the projection is keyed by.

## 2. Results on the slices [M]

**Best configuration: in-article experts 0–4, lr = 0.0005, silent at boundaries.**

| slice | record | gain | of total |
|---|---:|---:|---:|
| 1M | 126,430 B | **+30 B** | 0.024% |
| 10M | 1,264,639 B | **+702 B** | 0.056% |

**Variants on 10M** (gain over each run's own baseline):

| variant | gain |
|---|---:|
| in-article AU+AUR+AB1+AB2+ARU, lr 0.001 | +558 B |
| … lr 0.0005 / 0.00025 | +696 / +700 B |
| … lr 0.0005, silent at token boundaries (**best**) | **+702 B** |
| in-article unigrams only (AU+AUR) | +437 B |
| in-article bigram+trigram only | +191 B |
| rare-only unigram (ARU) | +273 B |
| rarity-weighted ARU (α = 1 / 0.5) instead of the df ≤ 2 filter | +664 / +643 B |
| **whole-file caches only** (GUR+GB1+GB2), lr 0.001 / 0.0005 + nobound | **−117 / +38 B** |
| in-article + whole-file, lr 0.0005 (+ nobound) | +633 (+673) B |
| StateMaps also keyed by record confidence | +334 B |
| weight set also selected by "cache agrees with the record" | +510 B |
| direct count inputs, stretch((n1+.4)/(n+.8)), besides the StateMaps | +422 B (+665 with nobound) |
| two-layer bank of 3 mixers (selectors: determinism, cache mass, candidate rarity) | +836 B, but its baseline is 2.2 KB *worse* than the record and the full system is worse than the record: rejected |

**In-article vs whole file:** the whole-file cache is worth about 0 alone and *reduces* the gain when added to the
in-article experts. This agrees with A1: the record already captures whole-file repetition. Only the in-article
restriction adds information.

**By content class** (best run, 10M):

| class | record B | gain B | of class |
|---|---:|---:|---:|
| prose | 806,498 | +452 | 0.056% |
| link target | 180,815 | +76 | 0.042% |
| ref/citation | 76,602 | +60 | 0.078% |
| external link/URL | 51,819 | +51 | 0.099% |
| template | 15,477 | +33 | 0.21% |
| table | 21,391 | +24 | 0.11% |
| image/file | 17,279 | +23 | 0.13% |
| heading | 10,941 | +18 | 0.17% |
| category | 4,156 | +16 | 0.39% |
| interlanguage block | 27,923 | −37 | −0.13% (one big "article"; a real model would switch off here) |
| metadata block | 10,260 | −23 | −0.22% |

**By in-article repeat and by rarity** (10M):

| token earlier in this article | record B | gain B | of subset |
|---|---:|---:|---:|
| 0 (first use) | 754,664 | **−1,437** | −0.19% |
| 1 | 130,997 | +922 | 0.70% |
| 2–4 | 122,398 | +711 | 0.58% |
| 5+ | 177,392 | +506 | 0.29% |

| gain per token (B) | gdf 1 | 2–9 | 10–99 | 100–999 | 1K–9.9K | 10K+ |
|---|---:|---:|---:|---:|---:|---:|
| first use | −0.0015 | −0.0023 | −0.0033 | −0.0036 | −0.0027 | −0.0015 |
| 1 earlier use | +0.030 | +0.030 | +0.024 | +0.013 | +0.0037 | +0.0008 |
| 2–4 | +0.017 | +0.026 | +0.020 | +0.011 | +0.0038 | +0.0008 |
| 5+ | +0.012 | +0.010 | +0.009 | +0.0056 | +0.0027 | +0.0002 |

- Repeats of **rare words** are where the cache helps: up to about 1.9% of their bits.
- Repeats of common words (gdf ≥ 10K) are worth almost nothing, because the transformer already predicts them.
- The gain sits on the 2nd to 5th coded byte of a token (+572 of 702 B), i.e. after the first code byte or the case
  flag. The first byte gains little, and boundaries only lose.
- By offset in the article: < 1K coded bytes −34 B, 1–4K +132, 4–16K +477, 16–64K +152, 64K+ −26.
- **History:** half 1 +411 B, half 2 +291 B. Per *cell*, though, half 2 is higher everywhere: the mixer learns to
  trust the cache more, and first-use losses grow too. The total falls only because the token mix shifts to common words.

**Why it falls short: the record already recalls in-article tokens better than a cache.** Per-token oracle on 10M,
summing the record's bits over a token's bytes and terminator, against the cache's own byte-level cost along the
same path, told for free that the token is in the cache:

| token earlier in this article | tokens | record bits/token | AU cache (told for free) bits/token | per-token oracle switch saves |
|---|---:|---:|---:|---:|
| 1 | 180,055 | 5.82 | 8.80 | 4,767 B |
| 2–4 | 198,994 | 4.92 | 8.65 | 4,611 B |
| 5+ | 507,867 | 2.79 | 6.50 | 7,872 B |
| all | 1,443,092 | 6.57 | | **17,251 B UPPER BOUND (1.4%)** |

The simulation realizes about 4% of this oracle. (A2 realized 14% of its same-article oracle on links alone.)
1M shows the same pattern: 6.21 vs 9.1 bits per once-repeated token, oracle 1,695 B, realized 30 B.

## 3. Full scale [M counts, ESTIMATE gain]

Token counts in full enwik9 (`e11_fullscale`, text-level tokens). Calibration: the first 1,325 pages give the same
(repeat × history-df) cell counts as the simulator's coded tokens, e.g. 91,112 vs 91,123 first-use never-seen tokens.

| repeat \ gdf | 1 | 2–9 | 10–99 | 100–999 | 1K–9.9K | 10K+ |
|---|---:|---:|---:|---:|---:|---:|
| first use | 1.10M | 1.56M | 3.54M | 9.37M | 18.9M | 23.8M |
| 1 | 0.17M | 0.34M | 0.87M | 2.14M | 5.10M | 11.4M |
| 2–4 | 0.07M | 0.26M | 0.76M | 1.76M | 4.46M | 14.0M |
| 5+ | 0.04M | 0.23M | 1.14M | 2.48M | 5.28M | 30.9M |

Full enwik9 has 139.6M page tokens, 100.8× the 10M slice. In-article repeat counts and gdf don't depend on article
order, so **the record's `-e` reordering doesn't change this table.** Reordering only changes history df, which the
projection doesn't use.

**Projection [ESTIMATE].** Multiply the 10M gain per token of each (repeat, gdf) cell by its full-scale token count:

| per-cell rates from | first use | repeats | total |
|---|---:|---:|---:|
| whole 10M slice | −137 KB | +211 KB | +74 KB |
| first half only | −129 KB | +191 KB | +62 KB |
| second half only (more mature mixer) | −146 KB | +235 KB | +90 KB |

- **Deflation for the warm CM and cheap full-scale content.** The same cell scaling applied to the record's own
  cost predicts about 126 MB for full enwik9. The real archive is 97.0 MB, so slice per-token costs are about 1.3×
  too high (cold CM, no census-type pages in the slice). If the cache's gain scales with what the record pays, the
  totals become **+57 KB (whole slice), +48 KB (half 1), +69 KB (half 2)**.
- A history-df keyed projection would be pessimistic and invalid: after 243K articles, "df ≥ 100" includes moderately
  rare words. It comes out near zero. That is why gdf is used.
- **Central estimate: about 60 KB, realistic range 45–90 KB.**
- **Loose ceiling:** if a real mixer gated away *every* first-use loss, the half-2 positive cells give 235 KB, or
  about **180 KB** deflated. The per-token oracle scaled the same way is about 1.3 MB (**UPPER BOUND**, unreachable).

**Which way the offline-vs-real bias goes:**
- **Upward:**
  - A real implementation feeds the record's 23 layer-0 mixers and final SSE. Their selector contexts are far
    richer, so they could gate part of the 137 KB of first-use losses. Gating half of it would put E11 near 100 KB.
  - The full run has 100× more training tokens for the mixer, and per-cell rates rose from half 1 to half 2.
- **Downward:**
  - The simulation mixes after the final SSE, which is the most favorable place for a residual model.
  - Every attempt here to gate the losses with richer selectors (record confidence, agreement, determinism/rarity
    banks) made things *worse* on 10M.
  - **E10 (dynamic evaluation, in flight) and E2 (online correction head) adapt the transformer to the current
    article, the same signal.** Dynamic evaluation is classically what captures the cache effect. On top of a
    combo with E10, E11's gain is likely to shrink.
- **Overall:** below the 100 KB bar on the central estimate. Clearing it needs the real mixers to gate the
  first-use losses far better than this simulation does, and on top of E10.

## 4. Runtime and RAM of a real implementation [ESTIMATE]

| item | estimate |
|---|---|
| State | Per-article tries or hashed tables for unigram/bigram/trigram plus counts. Largest article in the 10M slice: 85K unigram, 198K bigram, 301K trigram trie nodes. Fixed hash tables of 2^20 slots × 16 B per order (reset per page) cover far larger articles: **≤ 50 MB** |
| Work | Active only inside tokens (61% of coded bytes). One table lookup per expert per byte, a candidate scan (a few entries after the first byte), 5 StateMap lookups per bit, and 5 more inputs to the mixers. The simulation runs at 3.5 µs per coded byte unoptimized, including a second mixer and analysis. A compressor version is about 0.5–1 µs per coded byte: about 586M × 1 µs ≈ 10 min here, so **≈ 0.2 h on the reference machine** (≪ 6 µs per byte) |

Cost is not the problem; the size of the gain is.

## 5. Recommendation

**NO-GO on E11 as a standalone addition** (central ≈ 60 KB < 100 KB). Attic: `attic/2026-11-05-E11-in-article-cache/`.

1. **Cheapest decisive follow-up (offline, no new mechanism):** rerun `e11_sim` on a per-byte cost dump of the
   **E10 / C1 combo** build. It would show how much of the 702 B survives dynamic evaluation. If E11 still gives
   ≥ 0.05% on top of E10, revisit. If it shrinks (expected), close it.
2. If it survives, the only route over the bar is **gating the first-use losses inside the real mixers**. Try a
   real 10M run with AU+AUR+AB1+ARU (typed ≥ 1 only) as layer-0 mixer inputs, with the cache's state (deterministic,
   candidate count, rarity) as mixer *selector* contexts rather than inputs. The 10M check is ±0.06%, so expect a
   noisy answer.
3. Content signals that held up in both A2 and E11: **in-article recall of rare tokens** (+1–2% of their bits), not
   whole-file repetition and not candidate ranking. Any future specialist should target rare, bursty tokens, ideally
   through the transformer (E10-style adaptation, or a rare-token-aware head on E2) rather than as a parallel predictor.
