# AGENTS.md — sub100mil

Goal: a Hutter Prize style compressor whose total size **S = archive + decompressor**
on **enwik9** is under **100,000,000 bytes**, while staying within the official
compute limits. The authoritative constraints live in [`docs/RULES.md`](docs/RULES.md).
Read it before you design anything that touches time, memory, threads or the size accounting.

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
  - `enwik9_mid10M`: 10 MB from offset 5×10^8, a guard against overfitting to the front of the file
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
- Toolchain as of 2026-11-04: `gcc` and `python3` are present. `g++`, `clang` and `cmake`
  are **not** installed (`sudo dnf install gcc-c++ cmake` when C++ is needed).
