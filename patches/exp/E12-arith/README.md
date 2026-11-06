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

## Known limits

- Slice mode only so far. A full enwik9 run (`-e`, reordered articles, several blocks) needs the decoder to skip
  each block's WRT flag byte, not only the first one; check `art_` resets there before a full run.
- Article text is kept in a `std::string` per article and parsed with `std::regex` only at trigger points (literal
  suffix prefilter first).
