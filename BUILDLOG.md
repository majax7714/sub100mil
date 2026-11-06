# Build log

Newest entries first. One entry per session or experiment. Format:

```
## YYYY-MM-DD: short title
- **Did:** ...
- **Numbers:** slice → bytes (bpb), time, RSS. Ledger rows added: N
- **Learned:** ...
- **Next:** ...
```

---

## 2026-11-06: Batch 3 (off-plan darts): E12 arithmetic consistency is a GO; X1 cross-domain and A3 encoder-side selection are NO-GO
- **Type:** E12 model addition (non-transformer specialist); X1 model addition (non-transformer, cross-domain synthesis, offline); A3 architectural
  (asymmetric compute). The batch was deliberately off the transformer path: two non-transformer darts plus one new way of spending compute.
- **Did:**
  - **E12:** numbers that are functions of earlier numbers. The specialist is a final stage after SSE, causal, with its own WRT decoder. It covers
    census percentages k/P and k/H (a lattice of possible display values), last items summing to 100, housing density from population density,
    km²↔mi² and land/water area arithmetic, sex ratio, and unit conversions anywhere. Patches in `patches/exp/E12-arith/`, design in
    docs/design/E12-arith-consistency.md (commits d8c83bc, 93617a8, a1f5288, 9a947ea, 8934a27, 9870d53).
  - **X1:** surveyed 25 mechanisms from biology, physics and chemistry (docs/design/X1-cross-domain.md). Tested the top two offline: Potts/DCA
    couplings between distinct tokens of an article (from protein coevolution), and coarse-grained state as a final-stage context (from
    renormalization). Commit 8a3a0a2; attic `attic/2026-11-06-X1-cross-domain/`.
  - **A3:** the encoder forks the E2 head's state per block, tries K learning rates, commits the best and sends the choice; the decoder replays only
    the winner. Built a head-input dump and an exact offline replay of the head. Commits 1b4078b, 73f7026, 0f73794; attic
    `attic/2026-11-06-A3-encoder-select/`.
- **Numbers:**
  - **E12:** record cost on derivable slots on mid10M is 58.3 KB (17% of the archive). Offline sim mid10M −12,928 B (−3.83%), 10M −127 B.
    **Real build, census 1M slice (`enwik9_mid1M_pa`): 31,408 → 30,049 B (−4.33%)**, round-trip OK; enwik9_1M 126,473 → 126,469.
    **Full enwik9 ESTIMATE about 170 KB (140–190 KB)** from 1.22M slots counted on the full WRT stream. About 0.95 µs per byte (about 0.2 h,
    an estimate), under 1 MB RAM, about +3 KB of S.
    **Census 10M slice (`enwik9_mid10M_pa`): record 337,982 → E12 325,041 B (−12,941, −3.83%)**, round-trip OK; causal sim said −12,928.
    **10M: 1,264,682 → 1,264,559 (−123)**, round-trip OK; sim said −127. The sim tracks the real build to within 0.3%.
  - **X1:** couplings +396 to +423 B (0.03%) on 10M over a warm n-gram control; online on a partial 100M cost log +277 B at 24.7M coded bytes.
    Projected about 35 KB (10–60 KB). Coarse state ≤ 28 B on 10M.
  - **A3:** 10M oracle bound −149 B of head loss vs the best fixed LR (block 16K); greedy fork-and-commit loses (+410 B at 16K); lookahead nets
    −42 B at 30× encoder head cost. By-product: **C1 with E2's LR halved (`E2_A0=0.25 E2_AF=0.05`): 10M 1,261,970 (−45 B vs C1)**, round-trip OK.
  - Ledger rows added: 7 (E12: 1M, mid1M_pa ×2, 10M, mid10M_pa ×2; A3: 1).
- **Learned:**
  - The biggest non-transformer gap we've found is **arithmetic**: neither the CM nor the transformer can divide, and Rambot census articles are
    full of exact ratios. Gains concentrate on a well-defined 4% of enwik9.
  - Slices flatter CM-side vocabulary mechanisms: a token n-gram model pre-fed with 30K other enwik9 articles gains 3.2% on the cold 10M slice.
    Use X1's warm control (`x1_sim ... X1_WARM=...`) before trusting a CM-side slice gain.
  - Head-loss proxies overstate marginal changes to the E2 head about 5× (−427 B head loss became −45 B coded).
  - Encoder-side selection needs a component with a sharp, non-stationary optimum and real time slack; we have neither (7.8 h of headroom, flat
    LR optima).
  - `data/slices/enwik9_mid10M` can't round-trip through slicetool, which assumes the first 29 lines are the siteinfo header. E12 used slices
    starting 29 lines before a `<page>` (offset 500,000,168) in `build/exp/E12/`.
- **Next:** E12: add block-header handling for full-mode runs; try mixer lr 0.004 (−13.4 KB offline vs
  −12.9 KB); then a 100M check and a fold into C1. Regenerate the mid slice so it round-trips. X1's leftover idea: feed a coupling signal into
  E2's head instead of as a byte expert.

## 2026-11-06: E2 holds on 100M (−0.150%); first 100M number for the record
- **Type:** confirmation run (model addition E2)
- **Did:** Rebuilt both binaries from committed patches (the baseline was md5-identical to `build/refs`), checked that slice mode works at 100M,
  then ran the record and the E2 logit head on enwik9_100M (enwik8) one after the other through the main lane. Ledger commits 65798d3 and
  8fce6b5; E2 README updated.
- **Numbers:**
  - **record 100M: 12,006,323 B (0.9605 bpb)**, peak RSS 6,558 MiB.
  - **E2: 11,988,349 B, −17,974 (−0.150%)**.
  - Both round-trips OK.
  - Wall times are heavily contended (record compress took 21,482 s, E2 11,687 s), so they carry no speed information. E2's cost is still
    best taken from perf: about 5 µs per token, about 1 h at full scale (an estimate).
  - Ledger rows added: 2.
- **Learned:**
  - E2's relative gain declines slowly with length: −0.17% (1M), −0.168% (10M), −0.150% (100M), as the warming CM absorbs more of what the head
    corrects. Expect less than about −145 KB on full enwik9 (an extrapolation).
  - PPMD's temp file lives on the `/tmp` tmpfs. Point `BENCH_TMP` at disk before any full run (see EXPERIMENTS tooling debt).
- **Next:** confirm C1 (and later C2 with E10) on 100M; that's the number to project to full scale.

## 2026-11-06: E10 dynamic evaluation works, but only with decay, and its best setting is over the time budget
- **Type:** model addition (transformer-side, online learning of internal parameters)
- **Did:**
  - Step 0: confirmed that the record adapts nothing on the transformer side online. The weights are frozen int4, the "pretraining" is only
    for the CM models, and there are no biases and no learnable final norm.
  - Built online SGD (one step per token on the head's cross-entropy) on per-channel gains of all 24 branch outputs and per-neuron gains on the
    MLP relu² activations, i.e. dynamic evaluation of the quantization scales.
  - The gradient is truncated: residual paths exact, attention Jacobians dropped, MLP backward exact in the top `E10_MLPBK` layers using int16
    madd on the forward weight arenas.
  - Patches in `patches/exp/E10-dyneval/` (commits dcc7e6d, dfbae0f, f61bee1, 915a141, 1d3448b, 3caaaba). Comp and decomp need the same `E10_*`
    env vars.
- **Numbers:** 10M (record 1,264,682):
  - **bk12q4d (decay 1e-5): 1,261,985, −2,697 (−0.213%)**.
  - bk12q4 without decay: +66.
  - bk6q4 without decay: +1,050.
  - 1M: bk12h −452, bk12q4d −434, bk12q4d+E2 −537.
  - Cost on the reference machine (estimates): bk12q4d about 35 µs per token, so **about 5.7 h per direction**; bk6q4d about 3.3 h; bk12q4d+E2 about 7 h.
  - About 0.25 MB RAM, RSS unchanged, about +6 KB of S.
- **Learned:**
  - Without decay the parameters drift, and the gain fades from −0.40% at 1M tokens to −0.165% at 5.7M on the transformer-only proxy. Decay
    toward the frozen weights holds it at −0.375%.
  - Gradient quality through depth is the biggest lever: the exact MLP backward gives 2.6× the residual-only gain, and all 12 layers beat the
    top 2 by about 4×.
  - Biases and low-rank residual branches diverge. Adam lost to SGD.
  - It overlaps strongly with E2 (E2 adds −0.07% on the adapted transformer vs −0.30% on the frozen one), so it's more a substitute for E2
    than an add-on. At full setting it costs far more time per byte saved than E2.
- **Next:**
  - On 10M: bk6q4d (fits the budget) and bk12q4d+E2.
  - Tune decay on 100M with `proxy.sh`.
  - Cut the per-neuron backward cost (int4 transposed W_up axpy, or a backward every 2nd token).
  - A cheap attention Jacobian.

## 2026-11-05: C1 combination is the new best (−0.211% on 10M); the E9 bug fix is neutral
- **Type:** combination of model additions, plus a bug fix
- **Did:** E9 patch (fxcm's real final output into the final mixer and `auxiliary_context_`) and the C1 build (E1 gate3 + E2 logit head + E3
  agg16 BITLSTM + E9, with a merge patch; the BITLSTM reads E2's mixer column too). Patches in `patches/exp/E9-fxcm-final/` and
  `patches/exp/C1-combo/` (commits 0707157, 4557627, 4b8f859, 5b61307). Comp and decomp both need `E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1`.
- **Numbers:** 10M (record 1,264,682):
  - E9 alone: 1,264,688, +6.
  - **C1: 1,262,015, −2,667 (−0.211%)**, against a sum of parts of −2,859.
  - C1 without E3: 1,262,339, −2,343.
  - Peak RSS 5,811 MiB (+13).
  - About 15 µs per coded byte, so **about 3.1 h on the reference machine (an estimate; about 40% of the 7.8 h headroom)**.
  - The executable grows by 45 KB unpacked, about +15–20 KB of S after UPX (an estimate).
  - Ledger rows added: 7.
- **Learned:**
  - The small wins mostly add up (93% of the sum).
  - All the overlap is between E1 and E2, since both recalibrate how much the mixers trust the transformer. E1 keeps about −220 of its −410 on
    top of E2.
  - E3 is fully additive but the most expensive per byte saved. Drop it first if time gets tight.
  - E9's dead column was never missed.
- **Next:** confirm C1 on 100M (after the running 100M baseline and E2 runs). Try gating E2's mixer input by E1-style confidence. Then fold in
  E10, depending on its 10M results.

## 2026-11-05: E11 in-article cache model is a NO-GO, but the closest miss so far (about 60 KB)
- **Type:** model addition (non-transformer), tested offline only
- **Did:** Simulated in-article caches over WRT tokens (unigram, recency-decayed unigram, bigram, trigram, rare-token unigram; reset at every
  `<page>`), mixed per bit with the record's probabilities from A1's cost dumps, the same method as A2. Contrasted with whole-file caches.
  Scripts `scripts/analysis/e11_*`; write-up docs/design/E11-in-article-cache.md; attic `attic/2026-11-05-E11-in-article-cache/` (commit 45a62cd).
- **Numbers:** best variant 10M +702 B (0.056%), 1M +30 B. Whole-file caches −117 to +38 B. Projected to enwik9: **about 60 KB (45–90 KB),
  an estimate**, against the 100 KB bar. Cost would be fine: about 0.2 h, ≤50 MB. No ledger rows (no archive produced).
- **Learned:**
  - Only the in-article restriction helps, which agrees with A1.
  - The gain is on in-article *repeats* of *rare* tokens (1–3% of their bits). Common tokens gain about 0, and first uses in an article lose.
  - The record pays 5.8 bits for a token used once before in the article; the cache pays 8.8 even when told the token is in it. Oracle ceiling
    on 10M: 17.3 KB.
  - A2 and E11 both point to the same signal, in-article recall of rare, bursty tokens, which E10 and E2 also go after.
- **Next:** rerun `e11_sim` on a cost dump of the E10 or C1 build. If E11 still adds ≥0.05% on top of dynamic evaluation, build it into the
  compressor with the cache state as mixer selector contexts. Otherwise close it.

## 2026-11-05: A2 link-target model is a NO-GO (about 17 KB at full scale)
- **Type:** architectural (specialist model), tested offline only
- **Did:** Simulated a link-target candidate model (prefix tries over earlier link targets, titles and same-article links) mixed per bit with
  the record's probabilities from A1's cost dumps. Scripts `scripts/analysis/a2_*`; write-up in docs/design/A2-link-targets.md; attic
  `attic/2026-11-05-A2-link-targets/` (commits d000453, 2f3f897).
- **Numbers:** 10M +208 B (0.11% of link bits), 1M +16 B. Projected to enwik9 in the record's article order: about +17 KB (range 0 to +40 KB),
  against the 100 KB bar. No ledger rows (no archive produced, as for A1).
- **Learned:**
  - The record already ranks link targets far better than a candidate list: it pays 7.6 bits per previously seen link, while a frequency-ranked
    list pays 13.1 bits even when told for free that the target is in the list.
  - The only positive part is links already used *in the same article* (+3.7% on that bucket). The record still pays 6.5 bits for those, so its
    in-article recall is imperfect.
- **Next:** an in-article cache model over WRT word codes and phrases, which would cover prose (64% of bits) too and can be tested offline with
  A2's `BitSim`. Link structure is more promising as transformer context (LANDSCAPE D1) than as a byte predictor.

## 2026-11-05: E2 online correction head is the best result so far (−0.17%)
- **Type:** model addition (transformer-side, online learning)
- **Did:** Built 5 variants of an online correction on the frozen transformer, including ex-girlfriend's fast-weight head (pinned at e923fae via
  fetch_refs.sh). Patches in `patches/exp/E2-correction-head/` (commits cadd1b0, 568f489, 1901d62, 5a8ee71).
- **Numbers:**
  - **logit head** (a zero-initialized 205×193 matrix on the final hidden state, one step per token, its distribution fed as an extra mixer
    input): 1M −212, **10M 1,262,559 B, −2,123 B (−0.168%)**. About 5 µs per token, **about 1.0 h** at full scale, about 0.3 MB.
  - FW1 (fast weights on the last MLP block): 10M −2,165 B, but 2.6 h.
  - FW4 (ex-girlfriend's 4 blocks): −297 on 1M, but about 9.7 h, which **doesn't fit** the 7.8 h headroom.
- **Learned:**
  - Transformer-side corrections are about 5× bigger than the CM-side gains (E1, E3).
  - The gain did *not* grow with length: −0.21% on 1M and −0.17% on 10M. The naive full-scale extrapolation of about −165 KB is speculative and probably high.
  - ex-girlfriend's learning rates, tuned on neif, are too high for fx2-T.
  - Variants are selected with `E2_*` environment variables, which must match between compress and decompress.
- **Next:** Confirm on 100M. Test whether E1, E2, E3 and E9 add up. Try dynamic evaluation (online updates to the transformer's own weights).

## 2026-11-05: E1 transformer confidence as a context is a small gain (keep)
- **Type:** model addition
- **Did:** Tried 8 variants of feeding the transformer's confidence to the mixers and APMs. Patches in `patches/exp/E1-confidence/`
  (commits ef7ca4a, c464b49, bb671b9).
- **Numbers:** Best: **gate3**. Each of the 23 layer-0 mixers gets 28 extra inputs, which are copies of the transformer's stretched bit probability,
  each copy nonzero only in its confidence bucket (P(top byte | bits so far), top-1 probability, entropy, top-1/top-2 margin).
  1M −52 B; **10M 1,264,272 B, −410 B (−0.032%)**. Cost about 0.55 h on the reference machine, ≤26 MB RAM.
  Failures: confidence-selected mixer weight sets (+573 on 1M) and an extra APM (+186).
- **Learned:** New mixer contexts that start from zero weights lose, because the record's mixers learn slowly. Zero-initialized
  *additive* corrections (gating) win because nothing is lost while they learn.
- **Next:** Confirm on 100M or the full file. Try confidence-gating fxcm and PPMd too, and warm-starting new mixer contexts.

## 2026-11-05: E3 BITLSTM port (online-only) is a small gain
- **Type:** model addition
- **Did:** Ported neif's BITLSTM onto the record as an online-only logit correction right before the coder (neif's placement), with
  inputs from all 25 final-mixer inputs plus a 32-d sketch of the transformer's hidden state. Rewrote the forward/backward kernels
  (5× faster at 32 cells; only the summation order changes). Patches in `patches/exp/E3-bitlstm/` (commits 796d3e8, 0768a26).
- **Numbers:** 1M: −12 B at best (fast16). 10M: agg16 1,264,356 B, **−326 B (−0.026%)**. The gain per MiB rises from 14 to about 72–89 B by 5 MiB
  and hasn't levelled off. Feeding it as a mixer input instead is worse (+4 B on 1M), and 32 cells are no better than 16. Cost about 6.9 µs per coded byte,
  about 1.4 h on the reference machine (about 18% of our headroom), +1.4 MB RAM.
- **Learned:**
  - The online-only gain is small. neif's value came mostly from **replay-training on its own run**, which is CPU-only and so allowed under our plan.
  - The license combination (neif GPL-3.0-only + record GPL-3.0) should be fine, but the result could then only be distributed under exactly GPL-3.0. Flagged, not resolved.
  - Settings come from `BITL_*` environment variables, which must match between compress and decompress, and must be hard-coded for a submission.
- **Next:** Either CPU replay-training of the head on a trace of our run, or the online head on the 100M slice to see where it levels off.

## 2026-11-05: A1 retrieval model is a no-go (measured); per-byte cost tool
- **Type:** architectural (evidence only)
- **Did:** Built a per-byte cost instrument for the record (`patches/tools/per-byte-cost/`) and measured the match/retrieval
  headroom on the 10M slice and the full 586 MB stream. Write-up in `docs/design/A1-retrieval.md`, attic entry in `attic/2026-11-05-A1-retrieval/`.
- **Numbers:** The opportunity bucket is 2,071 B (0.16%) on 10M. A simulated retrieval mixer gives −101 to +18 B. The full-scale upper bound is
  about 1.0 MB; the realistic gain is about 10–20 KB.
- **Learned:**
  - Repetition is already captured, between the match models and the transformer's memorization. The bits go to word choice:
    prose 63.8%, link targets 14.3%. The transformer alone is only 0.9% worse than the full record.
  - **Free bug found:** the record reads fxcm slot 430 (a constant 0.5) instead of 428.
- **Next:** E9 (the slot fix), A2 (link-target model, testable offline first). Transformer-side word-choice work (E2, dynamic evaluation).

## 2026-11-05: record (fx2-cmix-transformer) built and profiled; experiment backlog
- **Type:** measurement (baseline characterization)
- **Did:** Built the record with g++ 15 (PGO+LTO, x86-64-v3; patches and build.sh in `patches/fx2-cmix-transformer/`). The rebuild is
  reproducible. Added a slice mode via 3 small patches. Write-up in `docs/refs/fx2-cmix-transformer.md`. Wrote `docs/EXPERIMENTS.md` (backlog).
- **Numbers:** 1M slice 126,473 B (1.0118 bpb), 10M 1,264,682 B (1.0117 bpb), both round-trip OK. About 105 s per MB in each direction.
  Peak RSS 5.8 GB on slices. Full-run ESTIMATE about 29.5 h per direction on this box. comp9 3,601,651 B vs the official 3,428,474 (UPX difference).
- **Learned:**
  - g++ 15 emits vendor-dependent RCPPS/RSQRTPS under fast-math even with `-mrecip=none`. That's the Intel/AMD desync bug.
    `-fno-finite-math-only` removes them, and build.sh now checks for it.
  - **The record's transformer is compute-bound** (IPC 3.0, 98 µs/token), unlike neif's L3-bandwidth-bound one. But it's sensitive to
    L3 capacity: +97% time with a 12 MiB co-runner. The rest of the compressor is DRAM-latency bound.
  - Time headroom is about 7.8 h to our 45 h target, but RAM headroom is only about 0.3 GB.
  - Slices sit inside the transformer's training data, which explains the 1.01 bpb.
- **Next:** Pick a first batch from docs/EXPERIMENTS.md (E1/E2/E3 are cheap model additions). Consider a full enwik9 baseline run of the record (about 30 h per direction, CPU only).

## 2026-11-05: cmix-neif built and profiled (reference, no changes)
- **Type:** measurement (baseline characterization)
- **Did:** Built neif with g++ 15.3.1 (`-march=x86-64-v3`, LTO, no PGO). Patch in `patches/cmix-neif/`; write-up in
  `docs/refs/cmix-neif.md`. Not slice-compatible (frozen 208-symbol alphabet, full-enwik9 metadata, order and dictionary), so
  the patch adds `NEIF_DUMP_STREAM`/`NEIF_STREAM_MODE` to code prefixes of its 552.75 MB preprocessed stream.
- **Numbers:** Stream prefixes: 1M 339,336 B, 10M 1,918,894 B (round-trip OK), 24M 4,209,622 B (compress only).
  Full-run ESTIMATE about 42.4 h per direction on this box (about 0.87× the official EPYC time). Peak RSS 9.55 GiB.
  Our self-extracting package is 4,569,502 B vs the official 4,000,689; the +569 KB is their packed stub, which needs clang.
- **Learned:**
  - **Time breakdown (perf, article window):** transformer 58.9% (153 µs/token), fxcm 24.2%, mixers/SSE 5.7%, LSTM 4.6%,
    PPMd 1.8%, BITLSTM 1.1%.
  - **The transformer is L3-bandwidth bound.** It reads about 11.2 MB per token, and its matmuls run at 112 GB/s, the
    measured single-core L3 bandwidth. Contention with an L3 co-runner makes it 2.25× slower per step.
  - The CM side causes about 87% of DRAM misses.
  - Prefixes under 8.81 MB are metadata or redirects and not representative; 24M is the first prefix that is mostly article text.
- **Next:** CPU-side candidates (no retraining): exact batched prompt-row matmul, int4-packed weights to shrink the L3
  footprint, prefetching for the CM DRAM misses, and spending the saved time on online correction models (BITLSTM gives about −0.3 MB
  for 1.1% of time). These are portable onto the record codebase.

## 2026-11-04: compute budget survey + reference clones
- **Did:** Cloned `third_party/cmix-neif-1.0` at 1c9ea5e and `third_party/fx2-cmix-transformer-v1` at 83f2603 (not built yet).
  Surveyed free and cheap GPU compute, written to `docs/COMPUTE.md` with cited prices and quotas.
- **Numbers:** Kaggle gives **30 GPU-h per week** (P100 or T4×2, 12 h sessions), plus a TPU v3-8 for 20 h a week. Modal gives $30 a month
  (H100 $3.95/h, B200 $6.25/h). That's realistically about 74 RTX-5090-equivalent hours a month, or about 130 at best, by spec-sheet speed only.
  Reproducing fx2-T's training (208 GPU-h) takes about 2.8 months of free quota. neif's (about 2,800 GPU-h) takes about 38 months.
  Renting a real 5090 on Vast.ai costs $0.31–0.49/h.
- **Learned:** Dense from-scratch training at record scale doesn't fit the budget. The user's direction is not to train
  dense models by default, though training small pieces is fine. Inheriting neif's GPL weights and working CPU-side is the leading candidate.
- **Next:** Pending the user's decision: read neif's build scripts, build it with AVX2, and profile where the time goes and whether it is bandwidth-bound or compute-bound.

## 2026-11-04: landscape survey (read-only)
- **Did:** Read-only survey of the record lineage and pending entries, written to `docs/LANDSCAPE.md`. Nothing cloned or run.
- **Numbers:** The record fx2-cmix-transformer is at 100,424,672. It was the one architectural jump, −10.4M vs fx2-cmix, from a
  6M-param offline-pretrained transformer with 4-bit weights stored in both binaries. Pending entries: cmix-neif 95,914,051,
  zmix 99,312,424, lexth11c 99,313,750.
- **Learned:** About 90% of cmix-neif's payload is ordinary article text at about 1.43 bits per stream byte, so the path to about 95M
  runs through the text model. Time is the binding constraint, not bytes: neif runs 48.7 h against a 53.6 h limit, with the
  transformer taking about 45% of it. Weights cost about 0.95 bytes of S per parameter. Every transformer direction needs GPU
  retraining: 208 GPU-h for the record entry, about 2,800 for neif. **`enwik9_mid10M` is unrepresentative**: it is mostly bot-generated
  US census town articles (8,695 "census" hits vs 96 in the front slice).
- **Next:** Replace mid10M with a strided slice (e.g. 10 × 1 MB at evenly spaced offsets). Clone and build cmix-neif at 1c9ea5e
  and fx2-cmix-transformer-v1 at 83f2603 (needs user approval). Decide on a GPU budget.

## 2026-11-04: benchmark harness
- **Did:** Added `scripts/bench.sh` (comp → decomp → cmp → ledger row, with `/usr/bin/time -v` wall time and RSS)
  and `scripts/README.md`. Installed g++ 15.3.1 and cmake 3.31.
- **Numbers:** 1M slice, xz -9e: 290,732 bytes (2.33 bpb), round-trip OK. 1 ledger row.
- **Learned:** The dev CPU (Ryzen 5 8600G) has AVX-512, but a prize reference machine doesn't, so all builds
  use `-march=x86-64-v3` and never `-march=native`. Timing only resolves to 0.01 s, which is fine for 10M+ slices.
  Cloning and building third-party reference compressors (fx2-cmix, cmix) was blocked by the auto-mode
  permission check and is waiting on the user's approval.
- **Next:** Fetch and build the reference CM compressors once approved. Measure them on 1M and 10M slices.

## 2026-11-04: project bootstrap
- **Did:** Created repo scaffolding: AGENTS.md/CLAUDE.md rules, build log, attic, results ledger,
  .gitignore for data/. Fetched enwik9, derived enwik8 and working slices (`scripts/fetch_data.sh`).
  Researched the official prize rules and compute limits into `docs/RULES.md`.
- **Numbers:** Baselines, single-thread, round-trip not checked: 10M slice xz -9e 2,720,256 (2.18 bpb), zstd -19 2,793,624, bzip2 2,916,026. 100M (enwik8): xz 24,831,648 (1.99 bpb), zstd 26,395,074, bzip2 29,008,758. 6 ledger rows.
- **Rules:** see docs/RULES.md. 1 core, about 50 h per program, 10 GB RAM, 100 GB disk, no GPU at runtime, compressor size counts toward S. Record L = 100,424,672 (fx2-cmix-transformer, Sep 2026). Prize needs S < 99,420,425. Benchmark entries under 100M already appear (unverified).
- **Learned:** Dev box has 12 cores / 30 GiB. No C++ toolchain installed yet.
- **Next:** Install g++/cmake. Set up a benchmark harness (compress → decompress → cmp → ledger row).
  Get a reference CM baseline on slices.
