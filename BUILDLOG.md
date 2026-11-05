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
