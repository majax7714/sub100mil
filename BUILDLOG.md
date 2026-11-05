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
