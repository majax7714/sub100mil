# A2: link-target model (NO-GO, 2026-11-05)

**Hypothesis:** wiki link targets are 14.3% of the record's bits, and never-seen ones cost 1.61 bits per char. A
specialist could predict `[[...]]` targets: at each typed byte, rank earlier link targets and article titles
that extend the prefix, using frequency, recency, same-article history and first-letter case folding, and feed
the next-byte distribution to the mixers. A second part would predict the anchor text after `|` from the target.

**What was done:** We measured offline before building anything, using A1's per-byte cost dumps; there were no new
compressor runs. Full write-up: [docs/design/A2-link-targets.md](../../docs/design/A2-link-targets.md).
Code: `scripts/analysis/a2_extract.py` (link/title/anchor spans in the coded stream), `a2_sim.cpp`
(prefix-trie experts plus a per-bit logistic mixer simulation against a record-only baseline, and a
byte-level linear mix) and `a2_fullscale.py` (seen-before statistics on full enwik9, in identity order and in the
record's `-e` article order). Commit d000453.

**Results [M]:**
- Simulated gain: 10M **+208 B** (+204 B on targets, +4 B on anchors). That is 0.11% of link bits and 0.016% of
  the total. 1M: +16 B. The byte-level linear mix loses 398 B.
- Split: same-article repeats +438 B, targets seen only in other articles −91 B, never-seen −143 B.
- On targets it has seen before, the record pays 7.6 bits per link. A frequency-ranked candidate set pays
  13.1 bits even when told for free that the target is in the set. The oracle per-link switch bound is 1.5 KB
  (global set) and 3.2 KB (same-article set) on 10M.
- Full enwik9: 8.65M links. Never-seen shrinks to 17%, seen only in other articles is 68%, same-article is 15%.
  Reordering barely changes this; earlier-title-only goes from 0.1% to 0.3%.
- **Projection [ESTIMATE]:** about +17 KB, range 0 to +40 KB. Loose ceiling about 280 KB.

**Why it failed:** The transformer is trained on enwik9 and reads the article's context, so it already picks the
right target far better than any prefix-frequency ranking. The set model mostly disagrees with it where the set
model is wrong. More history doesn't help: the per-link gain on seen links falls from the slice's first half to its
second half. Anchors rarely equal the target, and the record already codes them at 0.9 bits per byte.

**Cost, had it worked:**
- Same-article trie: <1 MB, <0.05 h.
- Global index: 150–220 MB (half the RAM headroom), 0.01–0.05 h.

**What we'd do differently / what it points to:**
- The only positive signal is **in-article recall**: the record still pays 6.5 bits for a link target already used
  in the same article. The general form is an in-article **cache model** over WRT word codes and phrases, covering
  prose as well as links. Test it offline with `a2_sim.cpp`'s BitSim over all word-code bytes before building anything.
- Use link structure as transformer *context* (LANDSCAPE D1), not as a byte predictor.
