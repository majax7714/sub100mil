# X2-timelaw: metadata-block time law ("radiometric dating") + redirect page-id law

Type: **model addition (non-transformer specialist, final stage)**. Base: fx2-cmix-transformer-v1 (the record) +
`patches/exp/E12-arith` (unchanged, as at HEAD). Date: 2026-11-06. Design, evidence and projection:
[docs/design/X2-cross-domain.md](../../../docs/design/X2-cross-domain.md) (§5, §9).

## Mechanism

phda9 moves every page's revision header into one block after the last page (`>DELTA`, `revision>`, `id>REVID`,
`timestamp>0YD:S`, contributor). `x2_timelaw.h` (copied into the tree as `src/models/x2_timelaw.h`) WRT-decodes the
coded bytes with its own small decoder, keeps only the current and previous line, and opens two kinds of slots:

- **time** (class 0): after `timestamp>`, when the record's revision id is ≥ 18.1M. Wikipedia issues revision ids
  in time order, so the time is interpolated between the nearest *earlier records'* ids below and above (a
  `std::map` of revision id → time). Candidates are integer times within ±min(2000, max(10, 8b)) of the
  interpolation, clipped to the neighbours' times, weighted exp(−|T − t̂|/b); b is the running mean of |error| per
  log2(neighbour gap + 1) bucket. Ids below 18.1M (renumbered by the 2005 import) are left alone.
- **rpid** (class 1): at a record's `>` after `/contributor>`, once 4 consecutive records had page-id deltas in
  [1, 64] (the redirects that follow the reordered articles in id order): the next 64 page ids no earlier record
  used, as deltas, weighted (1 + j)^−1.5.

`TimeLawMix` is E12Mix's final-stage mixer for these two classes (inputs stretch(p), bias, StateMap of p1,
stretch(p1); weight set per class × state × bit; lr 0.002). The predictor applies it **after** E12's stage:
`p = sse; p = e12->Predict(p); p = x2->Predict(p)`, and updates it after E12. It needs no order file and no tables, and it
works in slice mode and in -e/archive9 mode (the 6-byte coded block header is skipped like E12's).

## Files

- `x2_timelaw.h`: the model and the mixer (also used by `scripts/analysis/x2_csim.cpp`).
- `0001-x2-timelaw-predictor.patch`: `src/predictor.{h,cpp}` hooks (constructor after E12's, `Predict` after
  `e12_->Predict`, `Perceive` after `e12_->Update`). It applies on top of `patches/fx2-cmix-transformer/*.patch` +
  `patches/exp/E12-arith/*.patch` and needs E12's `PredictorOptions::e12_coded_block_header`.
- `build.sh [TREE] [OUT_BIN]`: pinned third_party + the record's patches + E12's patch and `arith.h` + this patch and
  `x2_timelaw.h`, then the record's PGO+LTO build and instruction guards. Default output `build/exp/X2/cmix_x2`.

## Runtime switches (comp and decomp must match)

| env | effect |
|---|---|
| neither | the record |
| `E12_DICT=<english.dic>` | E12 alone |
| `E12_DICT=… X2_DICT=<english.dic>` | E12 + X2 |
| `X2_DICT=…` only | X2 alone |

For a submission, the dictionary that -e/archive9 extract to `./.dict` would be used, as for E12.

```sh
patches/exp/X2-timelaw/build.sh
B=$PWD/build/exp/X2/cmix_x2 P=patches/fx2-cmix-transformer D=$PWD/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic
scripts/slot.sh --fast scripts/bench.sh --method fx2T+E12-arith+X2-timelaw --slice data/slices/enwik9_1M \
  --comp "E12_DICT=$D X2_DICT=$D FX2_BIN=$B $P/slice_cmd.sh comp {in} {out}" \
  --decomp "E12_DICT=$D X2_DICT=$D FX2_BIN=$B $P/slice_cmd.sh decomp {in} {out}"
```

## Stacking (C2 or any later combo)

It's a separate header and a separate `unique_ptr` in `Predictor`, so stacking means: apply the combo's patches, then
`0001-x2-timelaw-predictor.patch` (its context is E12's three hook lines; if a combo moves them, add the three
`x2_` lines next to E12's by hand), and copy `x2_timelaw.h` to `src/models/`. No E12 file is modified. If C2 makes E12
default-on instead of env-switched, do the same for `X2_DICT`.

## Results (round trips; ledger rows)

See the design doc §9: real benches on the 1 MB reordered-block proxies and enwik9_1M match the causal sim
(`x2_csim`) of this same header. On a 1 MB proxy the model only knows that proxy's ~2,800 records (cold), so
benches show 0.6–3.3 bits per timestamp. Seeded with the full run's earlier records, the same header gives 3.2–8.1 bits,
matching the offline sims. Full-enwik9 projection (real-model numbers): **≈ 127 KB, range 85–135 KB [ESTIMATE]**.

## Known limits

- **Bug, fixed in `patches/exp/C4-x2fix` (2026-11-11):** `x2_timelaw.h` here (and so C3-combo, C3-full, C4-combo)
  drops the page-id delta of every page with a `restrictions>` line (1,013 in the full run, drift +2,740,978), so the
  rpid law ran on a drifted base in all 70,571 slots and gained about nothing in the C3 full run; and `try_rpid()`
  reads `used_` at a negative index when the tracked id goes below −1 (segfault with other page orders). This
  directory's files are left as they were built; use `patches/exp/C4-x2fix/0001-x2fix.patch` for anything new.

- The rpid law under-performs its offline sim right after the article part (rank 175K proxy: 0.08 vs 1.01 bits per
  slot warm); it's a 5–7 KB term. (That was the bug above: fixed, the same proxy gives 1.00.)
- Not tested end to end in -e mode (enwik9-only pipeline); it rides on E12's planned full-mode run.
