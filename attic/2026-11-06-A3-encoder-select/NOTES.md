# A3: encoder-side selection with decoder replay (NO-GO, 2026-11-06)

**Idea:** the record is symmetric: the decoder repeats every computation of the encoder. Spend compute asymmetrically
instead. At each block of B tokens the encoder forks the state of a *cheap* component, runs the block K times with K
variants, keeps the best, commits its state and sends the choice (≤ log2 K bits per block). The decoder replays only
the chosen variant (1×). First target: the E2 online logit head, with K learning-rate multipliers m on its schedule.
Classification: architectural (a new way of spending the compute budget).

**What was built:** measurement only. No coder change was built, because the bound failed the decision rule
(≥ 380 B on 10M).
- `patches/exp/A3-encoder-select/0001-a3-dump.patch` + `dump.sh`: a transformer-only run dumps the head's inputs
  (`y`, `xn[192]`, `raw_logits[205]`) per token.
- `scripts/analysis/a3_sim.cpp`: an exact-op-order replay of the E2 logit head. Modes: independent trajectories,
  greedy fork-and-commit, fork-and-commit with lookahead, and replay of a choice file.
- `scripts/analysis/a3_oracle.py`: per-block oracle, side-info cost and a fixed-share mixture.
- Full write-up: [docs/design/A3-encoder-select.md](../../docs/design/A3-encoder-select.md).

**Results [M]** (10M slice, 5,763,179 head tokens, K = 10, m ∈ {0 … 4}; head-loss bytes; ×0.56 ≈ coded bytes,
calibrated on E2's own −2,123 B):
- Best fixed LR: m = 0.5, −427 B of head loss vs E2's m = 1. That is plain tuning.
- **Upper bound** (oracle over independent trajectories, net of adaptive side info, vs the best fixed): −42 (4K
  blocks), **−149 (16K)**, −127 (64K), −85 (256K), −48 (1M). That is ≈ −83 B coded at ×0.56, 4.6× below the bar.
  At the measured marginal factor of ≈0.11 (see the bench below) it is ≈ −16 B.
- **Fork-and-commit, greedy** (vs the best fixed, before side info): +1,648 (4K), +410 (16K), −6.5 (64K), +16 (256K).
  It *loses*.
- **Fork-and-commit with lookahead** (score = block + next H blocks at m = 0.5), net of side info: 4K/H4 +268,
  16K/H4 +42, 64K/H2 **−42** (≈ −24 B coded, the best realizable point, at 30× head cost in the encoder ≈ 30 h).
  K = 3 {0.354, 0.5, 0.707} at 16K/H4: −0.08 B before 58 B of side info.
- **Symmetric alternative**: a fixed-share mixture of all 10 heads (K× in the decoder too) gives −323 to −362 B,
  2.4× the selection bound. Soft per-token mixing beats hard per-block selection.
- 1M agrees: the best fixed m is 0.5; the oracle nets −23 B at 16K; greedy is worse at every block size.

**Why it failed:**
1. **Nothing to select.** The optimal LR of an online learner is flat and stable across blocks: 269 of 352 per-block
   winners lie in 0.354–0.707 around the global optimum 0.5. The oracle's gain over the best fixed setting is
   ≈ 4% of the head's own contribution, and most of it is selection on noise.
2. **Greedy selection is myopic.** What wins inside a block (a larger step fits local statistics; sometimes a frozen
   head) leaves a worse state for later blocks. A lookahead fixes the bias at (1+H)× encoder cost, and then gains
   ≈ 0.
3. **The asymmetry is small in this prize.** Both programs have the same ≈ 45 h limit and the record already uses
   37.2 h in each direction. The encoder's slack is only ≈ 7.8 h, so K ≤ 1 + 7.8 h / (component cost). Only
   sub-hour components qualify, and those are exactly the self-correcting learners whose hyperparameters are flat.

**Other targets for the mechanism (ESTIMATE, rule of thumb ≲ 4% of the component's own gain):** E3 LR ≲ 13 B;
E10 LR/decay ≲ 110 B but needs a transformer forward replay (unaffordable); final SSE/APM and layer-1 mixer rates
< 100 B each; layer-0 mixers and whole CM sub-model sets need ≫ 7.8 h. The only class where hard selection could beat
mixing is a discrete *state* decision a mixer cannot express: transformer context carry-over across article
boundaries, per-article preprocessing or WRT variants. These cost a transformer re-run or a two-pass encode, so they
too exceed the headroom.

**What we'd do differently:**
- Run the cheap non-stationarity check first (one `indep` pass, ≈ 5 min on 10M) before designing fork machinery.
- If a knob's per-block winners cluster at the global optimum, just retune it. Here: E2 logit LR × 0.5
  (`E2_A0=0.25 E2_AF=0.05`). **Bench [M]:** C1 10M 1,261,970 vs 1,262,015, so −45 B, round-trip OK (ledger
  `fx2T+C1-a3-lr05`). −427 B of head loss became only −45 B coded: head-loss proxies overstate marginal head changes
  by about 5×.
- Any speedup frees time in both directions, and spending it symmetrically (§2: K heads mixed) was worth more than
  selection. So revisit only for a discrete state decision that a mixer cannot express and that has a cheap proxy.
