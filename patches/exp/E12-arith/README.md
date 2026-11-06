# E12: arithmetic-consistency specialist ("numbers that are functions of other numbers")

Type: **model addition (non-transformer specialist)**. Base: fx2-cmix-transformer-v1 (the record), frozen weights.
Date: 2026-11-06. Design, evidence and projection: [docs/design/E12-arith-consistency.md](../../../docs/design/E12-arith-consistency.md).

## Mechanism

`arith.h` (copied into the tree as `src/models/e12_arith.h`) is a causal model over the coded (WRT) stream:

1. It WRT-decodes every coded byte with the same `english.dic` (its own decoder, A1's `wrt_decode` in C++) and keeps
   the text of the current article (reset at `<page>`).
2. When the text so far ends in a trigger (a left context such as `/km&&sup2; (` or `, and ` inside the race
   sentence), it opens a **slot**: a weighted set of candidate strings (number + the byte that ends it) derived from
   numbers earlier in the article. The main sources of information:
   - **lattices:** census percentages are `k/P` (race, Hispanic, age, 2 or 1 decimals) or `k/H` (households) for an
     integer k, so for small places only a few display values are possible; sex ratios are `100·M/(D−M)`;
     household size is `household population/H` (= P/H without group quarters);
   - **sums:** the last race item and the last age item are 100 − the others (± rounding);
   - **unit arithmetic:** mi² from km² (×1/2.589988), water = total − land, water % = W/T by interval arithmetic,
     population and housing density from land area, housing density = population density × units/people,
     decimal coordinates from the preceding D°M'S", general `N unit (` conversions with several roundings.
3. While a slot is open, each bit's prediction is the candidate mass consistent with the coded bits so far; once
   nothing is consistent it abstains.
4. `E12Mix` is a **final stage after the record's SSE**: outside slots the record's probability passes unchanged; inside
   a slot a small logistic mixer combines stretch(p_record), a bias, an adaptive StateMap of the specialist's p1
   (keyed by class, quantized p1, byte position, bit) and stretch(p1), with a weight set per (class, specialist state
   deterministic-0/deterministic-1/both/abstain, bit). It learns online; nothing is stored in the archive.

Determinism: plain double arithmetic and `snprintf` on already-coded data; the same binary compresses and
decompresses. The transformer override (`byte_mixer_output` exactly 0/1) still takes precedence.

## Files

- `arith.h`: the model and the final-stage mixer (also used by `scripts/analysis/e12_csim.cpp`).
- `0001-e12-arith-predictor.patch`: `src/predictor.{h,cpp}` hooks (constructor, after SSE in `Predict`, top of
  `Perceive`). Applies on top of `patches/fx2-cmix-transformer/*.patch`.
- `build.sh [TREE] [OUT_BIN]`: recreates the tree (pinned third_party + record patches + this patch + `arith.h`) and
  runs the record's build pipeline (g++ PGO+LTO, `-march=x86-64-v3`, AVX-512/EVEX, RCPPS/RSQRTPS and libmvec guards).

Runtime switch: `E12_DICT=<path to english.dic>` for **both** comp and decomp (unset = the record, bit-exact). For a
submission the dictionary the preprocessor already embeds would be used instead.

```sh
patches/exp/E12-arith/build.sh build/exp/E12/tree build/exp/E12/cmix_e12
B=$PWD/build/exp/E12/cmix_e12 P=patches/fx2-cmix-transformer D=$PWD/build/refs/fx2-cmix-transformer-v1/tree/dictionary/english.dic
scripts/slot.sh scripts/bench.sh --method fx2T+E12-arith --slice data/slices/enwik9_mid10M \
  --comp "E12_DICT=$D FX2_BIN=$B $P/slice_cmd.sh comp {in} {out}" --decomp "E12_DICT=$D FX2_BIN=$B $P/slice_cmd.sh decomp {in} {out}"
```

## Full mode (`cmix -e enwik9` / `archive9`)

- In -e mode runner.cpp codes preprocessor::Encode's 5-byte block header (type TEXT = 7, 4-byte length) before the
  WRT flag byte; in slice mode (-c/-d with --transformer) that header is stored raw. The patch passes this as
  `PredictorOptions::e12_coded_block_header = !g_raw_block_header`, and the model skips 6 leading bytes instead of 1.
  enwik9 is one segment (< 2^31 bytes), so there is no second block header.
- **Verified on the real -e stream:** `comp9 -e enwik9 --bytes-only --save-ppmd-bytes` (vocabulary indices, mapped
  back to bytes with kEnwik9Vocab) gives the 586,459,020-byte reordered stream (starts `07 37 af 12 70 07`). The
  model with the header skip opens exactly the same number of slots per class (and the same 51,870,944 in-slot
  bits) as on the identity-order slicetool stream: reordering moves whole articles, and the model is per article.
  Time 554 s over the stream on this box (0.94 µs per coded byte), the same as identity order.
- The dictionary: -e and archive9 extract it to `./.dict` in the working directory before the predictor exists, so a
  full run sets `E12_DICT=.dict` (both directions). A submission build would make that the default instead of an env
  variable.
- **Not tested end to end:** the -e pipeline is enwik9-only (hard-coded line counts and the 243,425-article order), so
  there is no smaller full-mode test. A full test is one comp9 build with this patch (the record's `construct.sh`)
  and one full compress plus decompress, about 30+ h per direction on this box, with `BENCH_TMP` on disk.

## Known limits

- Article text is kept in a `std::string` per article and parsed with `std::regex` only at trigger points (literal
  suffix prefilter first).
- Mixer lr: `E12_LR` (default 0.002; 0.004 measured better, see the design doc).
