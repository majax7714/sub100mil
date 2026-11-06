# AGENTS.md — sub100mil

Goal: a Hutter Prize style compressor whose total size **S = archive + decompressor**
on **enwik9** is under **100,000,000 bytes**, while staying within the official
compute limits. The authoritative constraints live in [`docs/RULES.md`](docs/RULES.md).
Read it before you design anything that touches time, memory, threads or the size accounting.

**Hard limits (summary; details and sources in RULES.md):** 1 CPU core, no GPU at runtime,
≤10 GB RAM (target ≤9.5e9 bytes peak), ≤100 GB scratch disk, ≲50 h per program on the
reference machine (target ≤45 h), AVX2 at most (no AVX-512), no network and no external files.
**The compressor's size counts toward S**, and any embedded weights or dictionaries count
too. Record L = 100,424,672 bytes (Sep 2026). A prize needs S < 99,420,425.

## Strategy stance

**We're chasing the compression goal, not improving the current record holder.**
- **Milestones:** first sub-100M, then about 95M.
- **Tuning existing code is allowed.** Forking the best open codebase and tuning it is a legitimate tool, and it's
  probably how we reach sub-100M first.
- **Big jumps need architectural change.** A jump like 100M → 95M most likely needs one: a new predictor
  class, a different way of spending the compute budget, or a new representation of the data. Tuning won't
  get there.
- **Keep at least one architectural bet in flight.** Don't spend all effort on tuning someone else's design.
- **Classify every experiment** as tuning, preprocessing, model addition or architectural in its build log
  entry, so we can see where our gains come from.
- See [`docs/LANDSCAPE.md`](docs/LANDSCAPE.md) for the survey of prior entries and the candidate directions.

### Plan of record (2026-11-04)
- **Many cheap mechanism attempts beat throwing money at it.** We'd rather make many diverse, cheap experiments
  that test different *mechanisms* than one expensive bet. Fast experiments on small slices come first.
- **No dense training by default.** Record-scale transformer training (208–2,800 GPU-h) doesn't fit our budget
  (see [`docs/COMPUTE.md`](docs/COMPUTE.md): about 74 RTX-5090-equivalent hours a month, free tiers only).
  Training *small pieces* is fine: correction heads, mixers, specialists, fine-tunes.
- **Use existing weights.** The record (fx2-cmix-transformer, the primary baseline because it's verified) and cmix-neif
  (pending) both publish GPL weights. We build on frozen weights and put the effort into CPU-side mechanisms.
  CPU time on the dev box is free, so it's the budget to spend.
- **Every GPU spend needs a cited estimate first.** Before any GPU job, write down the estimated hours and dollars per
  vendor, using the numbers in docs/COMPUTE.md (never estimate from memory), and get the user's OK.
- **Follow the evidence, but don't close doors (2026-11-05).** Batch 1 showed that transformer-side mechanisms
  beat CM-side ones by about 5×, so the transformer is the *current* winning path. That's an observation, not a
  constraint. Keep at least one non-transformer mechanism (specialist models, retrieval, preprocessing, new
  predictor classes) in every batch.
- **Third-party code** is fetched by script into gitignored `third_party/`, pinned to exact commits, and never
  vendored into the repo. Respect its license (most of the cmix lineage is GPL).

This file is the single source of project rules for any agent (Claude, Codex, etc.).
`CLAUDE.md` imports it.

## Working rules

1. **Commit small and often, straight to `main`.** This is a solo project, so no branches
   or PRs unless asked. One logical change per commit, with a message that says what changed and
   the measured effect, if any (e.g. `cm: add word model, 10M slice 2,431,002 -> 2,398,117`).
2. **Keep the build log.** Every working session and every experiment adds a dated entry
   to [`BUILDLOG.md`](BUILDLOG.md), newest first: what was tried, the numbers, and what's next.
   Write the entry in the same commit as the work.
3. **Never delete an attempt; attic it.** A strategy that failed or got superseded goes to
   `attic/<YYYY-MM-DD>-<slug>/` with its code (or the commit hash) and a `NOTES.md`
   covering the idea, results, why it failed, and what we'd try differently. Add a line to the
   index in [`attic/README.md`](attic/README.md). Negative results are results.
4. **Keep the docs current.** If a change makes README, AGENTS.md, docs/ or the build log
   wrong, fix them in the same commit. A stale doc counts as a bug.
5. **Record every measurement.** Each compression result we'd want to compare later
   goes into [`results/ledger.tsv`](results/ledger.tsv) (one row per run, append only).
   A number that isn't in the ledger didn't happen.
6. **Losslessness is non-negotiable.** No result counts unless decompression
   round-trips byte-exactly (`cmp` against the original). Always verify.

## Data

- `data/` is **gitignored** and never committed. Reproduce it with `scripts/fetch_data.sh`.
- `data/raw/enwik9` is the full 10^9 byte corpus. enwik8 is its first 10^8 bytes.
- Day-to-day work happens on **slices** in `data/slices/`:
  - `enwik9_1M`: smoke tests, seconds per run
  - `enwik9_10M`: the main iteration benchmark
  - `enwik9_mid10M`: 10 MB from offset 5×10^8. **Caveat:** this is mostly bot-generated US census town
    articles, so it's not representative. It's a stress test only, and a strided replacement is planned.
    It also **fails the slicetool round-trip** (slicetool drops an interlanguage link in its partial first article), so
    benches on it fail `cmp`. Kept only for comparing with old ledger rows.
  - `enwik9_mid10M_pa` / `enwik9_mid1M_pa`: 10 MB / 1 MB from offset 500,000,168, i.e. the same census region but
    starting 29 lines before a `<page>`, so slicetool round-trips. **Use these for mid-file benches.**
  - `enwik9_100M`: enwik8, the pre-full-run check
- Full enwik9 runs are expensive. Run one only when a slice result justifies it, and log
  wall time and peak RSS (`/usr/bin/time -v`).
- Checksums are in `data/CHECKSUMS.sha256`. Verify with `cd data && sha256sum -c CHECKSUMS.sha256`.

## Measuring

Report for every run: input slice, compressed bytes, bits per byte, compress and decompress
wall time, peak RSS, threads, git commit, and whether the round-trip was verified. Projections from
slices to full enwik9 are estimates; label them that way.

## Repo layout

```
AGENTS.md       project rules (this file)
CLAUDE.md       Claude Code entry point, imports AGENTS.md
README.md       project overview and current best
BUILDLOG.md     dated log of sessions and experiments, newest first
docs/           RULES.md (prize constraints) and design notes
src/            the compressor
scripts/        data fetch, benchmark and verification scripts
results/        ledger.tsv, the append-only measurement record
attic/          failed or superseded strategies, kept with notes
data/           corpus and slices (gitignored)
```

## Environment notes

- Dev box: 12 cores, 30 GiB RAM, Fedora 43. Our dev box is **not** the prize reference
  machine. Check docs/RULES.md for the time and memory limits and how timing is normalized.
- Toolchain as of 2026-11-04: `gcc`, `g++`, `cmake`, `python3` and `perf` (user-space profiling works without sudo)
  are installed (g++ 15.3.1, cmake 3.31). The dev CPU has AVX-512 but a reference machine does not,
  so always build with `-march=x86-64-v3`, never `-march=native`.
- Benchmark every run through `scripts/bench.sh` (see scripts/README.md).
