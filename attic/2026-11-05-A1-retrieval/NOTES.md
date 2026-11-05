# A1: long-range retrieval / match model over the coded file (NO-GO, 2026-11-05)

**Hypothesis:** enwik9's repetition (templates, boilerplate, bot articles) leaves room for a stronger
long-range match/retrieval model fed to the record's mixers. It would be CPU-only, deterministic, and need no training.

**What was done:** We measured before building anything. Full write-up with all tables: [docs/design/A1-retrieval.md](../../docs/design/A1-retrieval.md).
Tooling: the per-byte cost instrument in `patches/tools/per-byte-cost/` and match analysis in `scripts/analysis/a1_*`.
Commits 2f5a9e9 and f6873d4.

**Results:**
- 10M slice: bytes that a ≥32-byte match predicts correctly cost the record only 0.059 bits each (759 B total).
  The record's own match models agree on 99.3% of them, and the memorized transformer pays only 0.073 bits even
  for copies from ≥1 MB back.
- The opportunity bucket (match is right but the record pays ≥1 bit) is 2,071 B, 0.16% of the output.
- A simulated retrieval mixer on top of the record: −101 to +18 B.
- Full 586 MB stream: 12.2% of bytes are ≥32-byte-predictable, but nearly all from within the record's existing
  windows. The upper bound is about 1.0 MB; the realistic gain is about 10–20 KB (a judgement, not measured at full scale).

**Why it failed:** The record already captures repetition. Its match models handle local repetition, and the
transformer, having been trained on enwik9, has effectively memorized long-range copies. Expensive bytes are
expensive because of *word choice*, not missing copies.

**What we'd do differently / what it pointed to instead:**
- Transformer-side word choice (E2-style online correction, dynamic evaluation).
- A link-target model (never-seen targets are 10.9% of all bits at 1.61 bits/char).
- A free bug fix: the record reads fxcm output slot 430, a constant 0.5, as fxcm's final prediction; the real one is slot 428.
