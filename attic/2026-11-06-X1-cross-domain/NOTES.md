# X1: cross-domain "lucky dart" (NO-GO, 2026-11-06)

**Hypothesis:** physics, biology and chemistry have their own ways to predict and to exploit redundancy. Some of them
might map to predicting enwik9's bytes in a way the record (cmix CM + frozen transformer) doesn't already cover:
DNA compressors' substitution-tolerant and inverted-repeat models, protein coevolution (Potts/DCA couplings),
renormalization, max-ent/Ising, Kalman filtering, kinetic proofreading, immune affinity maturation, crystal symmetry,
spectroscopy, and so on.

**What was done:** offline only, on the record's per-byte cost dumps.
Full write-up with the 25-row candidate table: [docs/design/X1-cross-domain.md](../../docs/design/X1-cross-domain.md).
- `scripts/analysis/x1_targets.py`: the record's bits on the regions each mechanism would act on (sort keys, closers,
  headings, table separators, paragraph ends, dates, offset profile).
- `scripts/analysis/x1_sim.cpp` (fork of `e11_sim.cpp`):
  - a Potts / direct-coupling expert between distinct tokens of an article;
  - a warm-history pre-feed from the articles of full enwik9 *after* the slice;
  - a warm vocabulary control (unigram / n-gram);
  - a coarse-state APM.
- `scripts/analysis/x1_extract_prefix.py`: aux files for a prefix of a cost log that is still being written. It was
  used on a record cost log of the 100M slice (observation only, `build/exp/X1/run100M/`).

**Results [M]:**
- Target census on 10M:
  - category sort keys, the inverted-repeat analogue ("Warhol, Andy" from the title "Andy Warhol"), cost 215 B in total;
  - closers 0.7% of bits; headings 0.8%; table separators 0.01%; paragraph ends 0.19%;
  - the bpb is flat past the transformer's window.
  - Structural regions hold < 1.5% of the bits, and the record codes most of them at < 1 bit per coded byte.
- **Potts couplings** (when token A first appears, boost tokens B that co-occurred with A in earlier articles):
  - Cold online: 10M −12 B; on top of E11 −44 B. 1M −0 B; on top of E11 −7 B.
  - Warm pre-feed of 30K later articles: **+396 B (0.031%)** on 10M over a warm token n-gram control (+423 / +421 B with
    other settings). +29 B on 1M.
  - Over a unigram-only control: +1.9 to +2.1 KB (0.15–0.17%).
  - Online on a 12.6M prefix of the 100M cost log (3,170 pages of history, the record's own warmer CM as control):
    +31 B on top of E11; on a 24.7M prefix (5,817 pages) +277 B (0.005%), about 0.010% in the late deciles. It grows
    with history, roughly +0.005% per doubling.
- **Coarse state** (section heading / offset / line kind / nesting depth) as an APM context against a constant-state
  control: ≤ 28 B on 10M.
- **Side finding:** a plain token n-gram model pre-fed with 30K *other* enwik9 articles gains **3.2%** (40.5 KB) on the
  cold 10M slice. This is the size of the slice cold-start penalty for CM-side "vocabulary" mechanisms.
- **Projection [ESTIMATE]:** couplings about **35 KB (10–60 KB)** on enwik9; UPPER BOUND 150–160 KB (only if the CM knew
  nothing beyond unigrams). Coarse state about 3 KB.

**Why it failed:**
- The record already contains the cross-domain tools in their CS form: logistic mixing = max-ent, SSE = proofreading /
  gain control, column and bracket models = periodicity / conservation, recovering match = substitution-tolerant DNA
  models, and a transformer that memorized enwik9's conventions.
- The one uncovered signal (article-level co-occurrence between distinct rare tokens) is mostly explained by token
  n-gram context. What is left (~0.03%) overlaps E10/E2's topic adaptation.

**Cost, had it worked:** couplings ≈ 6–7 µs per coded byte (≈ 1.4 h on the reference machine) and 0.6–1.2 GB RAM, which
is over the record's ~0.3 GB headroom.

**What we'd do differently:**
- Feed the coupling signal to the transformer side (a bias on E2's head from the article's rare-token set) instead of
  as a byte-level expert, and measure it on a C1/E10 cost dump.
- Always pair a CM-side specialist with a warm control (`x1_sim ... X1_WARM=... gucnt=1 experts=224`) before believing a slice gain.
- The one untested candidate that isn't covered in principle is an online evolving context pool (immune affinity
  maturation). It needs the real mixers to test.
