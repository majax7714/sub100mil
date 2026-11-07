# C3-full: the C3 submission build and its full-enwik9 run

Type: **harness / packaging** (no model change). Base: C3 (`patches/exp/C3-combo`). Date: 2026-11-07.

C3 is benchmarked so far only in slice mode, with its switches set by environment variables. This directory turns
it into a submission-shaped build (nothing read from the environment or from external files) and runs it on the
real full-mode path: `comp9 -e enwik9` → `archive9`, then `./archive9` with no arguments.

## Files

- `mktree.sh TREE`: C3's source tree (C3-combo/build.sh steps 1–10, same order) + `0001-C3-bake-defaults.patch`.
- `0001-C3-bake-defaults.patch`: removes every env switch that changes coding (list below). E12/X2 get the dictionary
  from the runner (`PredictorOptions::spec_dict`): the `-c/-d` dictionary argument in slice mode, `./.dict` in
  `-e`, argument-less (archive9) and `-x` mode, where the binary has just extracted it from itself. E12/X2 are off
  when there is no dictionary (the `./cmix -d .dict.comp .dict` self-extraction subprocesses and comp9 construction),
  which is also what made the env build's full mode fragile (`E12_DICT=.dict` would have been inherited by
  those subprocesses before `.dict` exists).
- `0002-costlog-on-C3.patch`: `patches/tools/per-byte-cost` ported onto the baked tree. Same 40 B record format
  (version 1, `costlog.py` reads it). One change: E3's BITLSTM correction is applied in the coder after
  `Predict()`, so the coder reports the probability it really coded with (`CostLogCoded`), and `cost`/`pbit` are
  the true coded costs (after SSE, E1 APM, E12, X2, the override and E3). Observation only.
- `build.sh`: builds `build/exp/C3-full/bin/{cmix, cmix_costlog, comp9, comp9_costlog}` (PGO+LTO, x86-64-v3,
  the record's guards, fails if any `getenv` other than the diagnostic `FX2_PRINT_VOCAB` remains).
  `comp9_costlog` carries the same comp_dict/comp_order/weights/header as `comp9`. `bin/comp9.parts` = sizes.
- `full_cmd.sh comp|decomp in out`: bench.sh wrapper for full mode (see its header). comp runs `comp9_costlog -e`
  under `env -i` (only `FX2_COSTLOG` passes) and then swaps the clean `cmix` in as archive9's decompressor; decomp
  runs that `./archive9` under `env -i`. So the round trip checks the submission decoder **and** that the costlog
  twin produced the submission's archive.
- `fullstream_check.cpp`: runs E12 + X2 exactly as the -e build constructs them over the real full coded stream
  (`build/exp/E12/full/e9.stream`) with p_in = 0.5: a crash/time/slot-count check, not a size estimate.

## Baked switches (C3's 10M 1,261,700 / 1M 126,244 settings)

| was (env) | now |
|---|---|
| `E2_LOGIT=1`, `E2_A0=0.25`, `E2_AF=0.05` (E2_FWK=0, E2_FINAL=1, K0/KF/TAU/ATAU defaults) | constants in `Predictor::Predictor` |
| `BITL_LR_E6=3000`, `BITL_ACCUM=1` (BITL=1, MODE 0, CELLS 16, NHID 32, SEQ 1024, FASTBW 1, STATS 0, DROP 0) | constants in `bit_head.cpp` |
| `E12_DICT=english.dic`, `E12_LR=0.004` | on iff a dictionary is in use, path from the runner, lr 0.004 |
| `X2_DICT=english.dic` | same as E12 (lr 0.002 as before) |
| `E9_FXCM_FINAL` (default on) | always on |
| E1 gate3 | already compile-time (`-DE1CONF -DE1_GATE=3`) |

## Reproduce

```sh
patches/exp/C3-full/build.sh
# 1M check, no env (must equal fx2T+C3-v1 126,244):
env -i PATH=/usr/bin:/bin HOME=$HOME BENCH_TMP=$PWD/build/exp/C3-full/benchtmp FX2_BIN=$PWD/build/exp/C3-full/bin/cmix \
  scripts/slot.sh --fast scripts/bench.sh --method fx2T+C3-full-clean --slice data/slices/enwik9_1M \
  --comp 'patches/fx2-cmix-transformer/slice_cmd.sh comp {in} {out}' --decomp 'patches/fx2-cmix-transformer/slice_cmd.sh decomp {in} {out}'
# full run: see build/exp/FULL/STATUS.md for the exact launch command
```

## What the full run must answer

1. **Actual S** = submission archive9 (`comp.bin`) + comp9 (un-UPX'd here; UPX/packaging is a separate item).
2. **Peak RSS vs the 10 GB limit.** The record peaks at 9.66 GB; C3 adds about 20 MB plus PPMd growth, already
   above our 9.5e9 target.
3. **Whether E12 + X2 deliver their projected ~300 KB** (E12 ≈ 170 KB, X2 ≈ 127 KB, ESTIMATES) against the warm,
   reordered full-run CM. The cost dump answers it per class offline (E12/X2 slots via `x2_csim`/`e12_csim`-style
   replays on the dump).
4. Wall times (contended; not usable for the time budget, use the perf-based estimates).

The per-byte cost dump (`build/exp/FULL/cost_full.bin`, ~23 GB) is the reusable product: future specialists can be
simulated against real warm full-file costs instead of rerunning 30+ h per idea.

## Results

(see below; filled in by the run)
