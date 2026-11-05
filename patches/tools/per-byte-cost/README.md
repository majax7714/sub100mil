# per-byte-cost: what the record pays for every coded byte

A patch for fx2-cmix-transformer-v1 (the record) that writes, for every byte the arithmetic coder
codes, the bits the record paid for it, the bits three major components would have paid alone, the
coder's eight per-bit probabilities, and the state of the record's match models. It is shared
tooling: use it to see *where* an experiment gains or loses bits, not just how many.

It is observation only. It reads values the predictor already computes and never feeds anything back.
With the log enabled, the 1M slice gives the same archive size as the release binary (126,473 B) and
round-trips. The 10M check is in docs/design/A1-retrieval.md. The log is off unless `FX2_COSTLOG` is set.

## Build

```sh
patches/tools/per-byte-cost/build.sh [OUTDIR]     # default build/exp/costlog; ~30 s
```

This copies the pinned `third_party/` tree to `OUTDIR/tree`, applies `patches/fx2-cmix-transformer/*.patch`,
then `0001-per-byte-cost-log.patch`, and builds `OUTDIR/cmix_costlog`. The flags are the record's
(g++, `-march=x86-64-v3`, `-DSEED=923 -DUPDATE_LIMIT=3000`). There's no PGO, which doesn't change the output.
The record's AVX2 and RCPPS guards are re-run. To instrument your own experiment, apply the patch on top of
your tree. It touches only `predictor.{h,cpp}`, `models/match.h` and `models/fxcmv1.{h,cpp}`, and adds
no state that predictions read.

## Run

Set `FX2_COSTLOG=<path>` for the **compression** run only. The decoder would write an identical file.

```sh
B=build/exp/costlog/cmix_costlog; P=patches/fx2-cmix-transformer
scripts/slot.sh scripts/bench.sh --no-ledger --method 'costlog' --slice data/slices/enwik9_10M \
  --comp   "FX2_COSTLOG=$PWD/cost_10M.bin FX2_BIN=$PWD/$B $P/slice_cmd.sh comp {in} {out}" \
  --decomp "FX2_BIN=$PWD/$B $P/slice_cmd.sh decomp {in} {out}"
python3 patches/tools/per-byte-cost/costlog.py cost_10M.bin      # summary
```

Overhead: 40 bytes per coded byte on disk (10M slice: 5.76M coded bytes, 231 MB) and a `log2` per bit per
component, which is negligible next to the transformer.

## Format

The file starts with a 16-byte header: `"FX2COST\0"`, then u32 version = 1, then u32 record size = 40. One
little-endian record per coded byte follows, in coding order. `costlog.py` has the numpy dtype and a loader.

| offset | type | field | meaning |
|---:|---|---|---|
| 0 | u8 | `byte` | the coded byte: the WRT stream (`preprocessor::Encode` output minus its 5-byte block header), not the raw XML |
| 1 | u8 | `cm_len` | cmix `Match` models (10 of them): longest current match length **in bits** (0–255) |
| 2 | u8 | `cm_pred` | that model's expected byte |
| 3 | u8 | `fx_len` | fxcm MatchModel2 best candidate length (rebased: verified bytes − 4; 0 = none) |
| 4 | u8 | `fx_pred` | its expected byte |
| 5 | u8 | `sm_len` | fxcm SparseMatchModel length (0 = none) |
| 6 | u8 | `sm_pred` | its expected byte |
| 7 | u8 | `flags` | bit 0: the fxcm best candidate is in delta/recovery mode; bit 1: the transformer override fired on some bit (`byte_mixer_output` exactly 0 or 1) |
| 8 | u16[8] | `pbit` | P(bit=1) exactly as the coder discretized it (`1 + 65534·p`, /65536), MSB first |
| 24 | f32 | `cost` | bits the record paid, computed from `pbit` (the archive is this sum plus a few bytes of header and flush) |
| 28 | f32 | `cost_tf` | bits under the transformer alone (the byte mixer's bit probabilities from the transformer's 205-way distribution) |
| 32 | f32 | `cost_fx` | bits under fxcm's own final prediction (its last output, the APM average) |
| 36 | f32 | `cost_ppmd` | bits under PPMD (order 25) alone |

The match-state fields are snapshotted at the first bit of the byte, i.e. what the models predicted before
seeing any of it. Component probabilities are clamped to [2^-16, 1 − 2^-16].

## Mapping coded bytes back to text

The coded stream is the WRT (word-replacement) transform of `slicetool pre` output. Words become 1–3 byte
dictionary codes ≥ 0x80, and there are case flags 0x40/0x07/0x06, the escape 0x0C, and a few swapped
punctuation characters. `scripts/analysis/a1_analyze.py` (`wrt_decode`) inverts it. For every coded byte it
also returns the position of the text character the byte produces, and it checks the result
byte-for-byte against the `slicetool pre` file. Use it to attribute costs to text. The same script has
a documented content classifier (prose, links, templates, tables, infobox, refs, metadata block, ...).

## Caveats

- `cost_fx` is fxcm's real final output. The record's own `Predictor` reads `model_predictions[430]` as
  "fxcm's output" (`fxcm_model_index`, used for the layer-1 input and `auxiliary_context_`), but fxcm
  fills fewer outputs than `num_models`, so that slot is a constant 0.5. The tool's first build logged
  exactly 8.000 bits per byte through it. See docs/design/A1-retrieval.md.
- Costs are per *coded* byte. To convert to bits per original byte, divide by text bytes, as the class
  tables in the A1 doc do.
