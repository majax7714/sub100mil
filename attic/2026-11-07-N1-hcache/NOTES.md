# N1: hidden-state continuous cache / kNN-LM on the transformer (NO-GO, 2026-11-07)

**Idea:** the record's transformer adapts nothing online. Store (key = its 192-d final normalized hidden state
`xn_i`, value = the token it predicted, `y_i`) for the last W tokens and form a cache distribution
`p_c(w) ∝ Σ_i exp(θ (cos(k_t, k_i) − max cos)) [y_i = w]` (Grave et al. 2017, continuous cache; Khandelwal et al.
2020, kNN-LM), combined with the C3 E2 logit head or with the coder's final model. Classification: model addition
(transformer-side). The prediction unit is a token = one byte of the WRT stream (205-symbol vocab; 10M slice =
5,763,179 head-predicted tokens, 1,330 piece starts excluded), so this is a byte-level cache keyed on the model's own state.

**What was built:** offline simulation only (no coder build; the sim was below the ≥ 300 B bar).
- `scripts/analysis/n1_sim.cpp`: replays the E2 logit head exactly at C3's rates (a0 0.25, af 0.05, atau 8000) on an
  A3 head-input dump (`patches/exp/A3-encoder-select/dump.sh`; 10M dump re-made, identical losses to A3's), and adds
  the cache. Keys: xn, optionally centered by a causal EMA mean (`N1_CENTER=1`, needed: raw hidden states are
  anisotropic, mean max-cos 0.86) and/or projected to KD dims by a fixed seeded Gaussian matrix. Three online,
  causal combinations, each gated by 64 contexts (max-cos bucket × cache-confidence bucket):
  - **LIN**: `(1−λ_g) p_head + λ_g p_c` (head-level proxy)
  - **LOG**: `p ∝ p_head · (p_c + 1e−3)^β_g` (cache log-probs added to the head's logits)
  - **FIN**: `(1−λ_g) p_rec(y) + λ_g p_c(y)` with `p_rec = 2^−cost` from the record's per-byte cost dump
    (`build/exp/A1/runs/cost_{1M,10M}.bin`, aligned greedily by byte). A byte-level linear mixture with the coder's
    final model is exactly decomposable bit by bit, so this is a direct coded-bytes proxy for a final stage after SSE
    (as E12/X2 are). Caveat: the record's final model, not C3's (C3 already has the E2 head).
  - Also an LSH "hashed hidden-state context model" (`N1_LSH=b` sign bits → adaptive counts).
- Raw outputs: `sim_out/` (s<slice>_w<W>[_kd<KD>]_c<center>.txt).

**Results [M, offline]** (bytes; head-level = vs the C3-rate E2 head; FIN = vs the record's coded cost):

| slice | config | cache alone | LIN | LOG | FIN |
|---|---|---:|---:|---:|---:|
| 1M | W 512, raw keys, θ 16 | 301,920 (head 126,734) | −12 | (lr bug) | −1 |
| 1M | W 2048, centered, θ 16 | 233,788 | −15 | +4 | −4 |
| 1M | W 8192, centered, θ 64 | 340,639 | −14 | −72 | −7 |
| 1M | LSH 12/14/16 bits | 409K–492K | ≥ 0 | ≥ 0 | ≥ 0 |
| 10M | W 2048, centered, θ 64 | 4,158,902 | −168 | −248 | −39 |
| 10M | W 8192, centered, θ 64 | 3,515,032 | −1,049 | **−1,504** | **−138** |
| 10M | W 8192, KD 32, centered, θ 64 | 3,445,600 | −849 | −1,246 | **−83** |

(10M: static head 1,275,587, C3-rate head 1,271,375, record coded 1,264,602.) A W = 32768 run on 1M died without output.

**Translation to coded bytes (ESTIMATE):** FIN, the direct proxy, gives −83 B (affordable KD 32) to −138 B (full
192-d keys) on 10M. The head-level LOG −1,250 to −1,500 B at A3's measured marginal factor ≈ 0.11 is ≈ −140 to
−165 B, consistent with FIN; even at E2's average factor 0.56 it would be ≈ −700 to −840 B. Bar: ≥ 300 B coded on 10M.

**Cost (ESTIMATE):** W 8192 × 192-d is 1.57M MAC + 8K exps per token, ≈ 10× over the 4 µs/byte budget. KD 32 int8
keys: 262K MAC + 8K exps per token ≈ 20K cycles ≈ 3.3 µs/byte on the reference machine, 256 KB of keys; i.e. the
whole remaining time headroom for ≈ −83 B.

**Why it failed:**
1. **The transformer already has the in-context information.** It attends over the whole article (128K-token pieces),
   and its tokens are WRT codes, so in-article word repeats are single tokens it copies itself. The cache alone is
   2–3× worse than the head (≈ 3 bits/token), and its gain lives almost entirely at max-cos 0.88–0.94.
2. **The CM side already does cross-article copying.** Head-level gains (−1.5 KB) shrink ≈ 10× against the full
   record (FIN −138 B): match models and PPMd order 25 cover what the cache finds beyond the transformer's context.
3. **What helps scales with W, and W costs linearly.** FIN went −39 (W 2K) → −138 (W 8K) on 10M; the gain is
   cross-article memory, which needs W ≫ article length, i.e. an approximate-NN index, not a brute-force window.

**What we'd try differently:**
- If revisited: an ANN over the *whole* history (LSH buckets of centered, projected keys holding candidate lists,
  then exact rescoring of ≤ 64 candidates), so W is unbounded at O(64·KD) per token; the W trend (−39 → −138) is
  the only lever. Plain LSH counts (no rescoring) were worth nothing.
- Feed the cache as an extra ByteMixer slot rather than into the head (not simulable offline; needs a real build).
- Re-simulate FIN against C3's full enwik9 cost dump when it exists (the warm, reordered CM is the real competitor).
