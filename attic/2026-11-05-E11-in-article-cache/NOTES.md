# E11: in-article cache model (NO-GO, 2026-11-05)

**Hypothesis:** A2 found that the record still pays 6.5 bits for a link target already linked in the same article.
A classic cache model over the WRT tokens the *current article* has already used (unigram counts and recency,
bigram/trigram continuations, rare-token weighting, reset at every `<page>`) would recover bits across all content,
prose included (64% of bits).

**What was done:** We measured offline only, on A1's per-byte cost dumps (no compressor runs), with A2's per-bit
mixing method. Full write-up: [docs/design/E11-in-article-cache.md](../../docs/design/E11-in-article-cache.md).
Code (scripts/analysis):
- `e11_extract.py`: per-byte article id and class.
- `e11_sim.cpp`: caches, StateMaps, a mixer against a record-only baseline, a per-token oracle, projection tables.
- `e11_tokhash.py` + `e11_fullscale.cpp`: global document frequency per token, full-enwik9 repeat×df tables.
- `e11_order.py`: the `-e` order.

**Results [M]:**
- Best (in-article experts, lr 0.0005, silent at token boundaries): **10M +702 B (0.056%)**, 1M +30 B (0.024%).
- Prose +452 B (0.056% of prose), link targets +76 B. Templates, categories and headings gain 0.2–0.4% of their small totals.
- Whole-file caches: −117 to +38 B. Adding them to the in-article experts lowers the gain. Only the in-article
  restriction adds information (agrees with A1).
- In-article repeats gain +2.1 KB, but first uses in the article lose −1.4 KB. Repeats of rare tokens
  (in < 100 pages of enwik9) gain 1–3% of their bits; repeats of common tokens gain about 0.
- The record pays 5.8 bits for a token used once before in the article. The cache, told for free that the token is in
  it, pays 8.8 bits. The per-token oracle switch bound is 17.3 KB on 10M (1.4%); the simulation realizes 4% of it.
- Richer gating (confidence-keyed StateMaps, agreement selector, 2-layer bank) only made it worse.
- **Projection [ESTIMATE]:** per (repeat × global-df) cell, scaled to 139.6M full-scale tokens and deflated 1.3× for
  the warm CM, about **60 KB (45–90 KB)**. If every first-use loss were gated away, the loose ceiling is about 180 KB.

**Why it failed:** the transformer (window 1024 + KDA) already recalls in-article tokens better than a cache, except
for rare words. The cache can't tell when a token is a first use, so its losses on first uses cancel most of what it
gains on repeats. The bar is 100 KB; the central estimate is about 60 KB.

**Cost, had it worked:** ≤ 50 MB, ≈ 0.2 h on the reference machine. Cheap, so cost was not the issue.

**What we'd do differently / what it points to:**
- Rerun `e11_sim` on a cost dump of the E10/C1 build. Dynamic evaluation adapts the transformer to the article,
  which is the same signal. If E11 keeps ≥ 0.05% on top of E10, try it in the real mixers, with the cache state as
  *selector* contexts so the real layer-0 mixers can gate the first-use losses.
- The signal that held up in A2 and E11 is in-article recall of **rare, bursty tokens**. Target it through the
  transformer (E10/E2-style online adaptation, or a rare-token-aware head) rather than with a parallel byte predictor.
