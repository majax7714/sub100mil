# C1: E1 gate3 + E2 logit head + E3 agg16 BITLSTM + E9, in one build

Type: **model addition** (combination of three model additions and one tuning fix). Base: the record
(fx2-cmix-transformer-v1 @83f2603 + `patches/fx2-cmix-transformer/*.patch`), frozen transformer weights.
Date: 2026-11-05. Question: do the three small wins add up?

**Answer: mostly yes.** On 10M, C1 gains −2,667 B (−0.211%), against −2,859 B for the sum of the three parts
measured alone. That is 93% of the sum. All of the ≈190 B overlap is between E1 and E2. E3 is fully additive (−324 inside C1)
and E9 is neutral.

## What is combined

| Part | Mechanism | Setting | Alone on 10M |
|---|---|---|---:|
| E1 gate3 | 28 extra layer-0 inputs: the transformer's stretch(p), gated by confidence buckets | compile-time `-DE1CONF -DE1_GATE=3` | −410 |
| E2 logit head | online softmax-regression correction head on the transformer's final hidden state, own byte slot into layer 0 and the final mixer | `E2_LOGIT=1` (a 0.5→0.1, τ 8000, `E2_FINAL=1`: defaults) | −2,123 |
| E3 agg16 | online 16-cell BITLSTM logit correction after the SSE (mode 0) | `BITL_LR_E6=3000 BITL_ACCUM=1` (`BITL=1`, 16 cells, mode 0: defaults) | −326 |
| E9 | fxcm's real final (output 428) to layer 1 and `auxiliary_context_` | on by default (`E9_FXCM_FINAL=0` turns it off) | +6 |

## Build

`build.sh` creates `build/exp/C1/tree` and builds `build/exp/C1/bin/cmix` (414,880 B unpacked, against the
record's 369,792). It takes about 1 min. It applies, in order:

1. `patches/fx2-cmix-transformer/*.patch`
2. `patches/exp/E1-confidence/*.patch` (applies cleanly)
3. `patches/exp/E9-fxcm-final/*.patch` (applies cleanly)
4. `patches/exp/E2-correction-head/*.patch`, **except** `src/predictor.*`. This covers the transformer side:
   `model_opt`, `arena_build`.
5. `patches/exp/E3-bitlstm/*.patch`, **except** `src/predictor.*`. This covers `bit_head`, the coder hooks, the
   makefile rule and the `last_hidden()` accessor.
6. `0001-C1-predictor-merge-E2-E3.patch`: E2's and E3's `predictor.{h,cpp}` hunks, merged on top of E1+E9.

Both E2 and E3 conflict with E1 in `src/predictor.{h,cpp}`: the same member list, `GetNumModels`, the
layer-0 input block and the final-mixer block. The resolution keeps both sides everywhere. Layer-0 inputs end
with: byte mixer, E1's 28 gated copies, E2's slot. There is one real decision:

- **The BITLSTM sees E2's final-mixer column.** E3 feeds the head "all final-mixer (stage-1) inputs". In C1 that
  is 26 columns (23 mixers, fxcm, transformer, E2 head), not E3's 25. The head reports
  `71 inputs (26 of 26 stage-1 cols ...)`.

The pipeline is the record's: g++ 15 PGO+LTO, `-march=x86-64-v3`, plus the AVX-512/EVEX, RCPPS/RSQRTPS and
libmvec guards, all of which pass. The PGO training run uses the C1 env, so the profiled paths are the ones C1
runs. `bit_head.o` is built without PGO/LTO/fast-math, as in E3.

## Running: the env must be identical for comp AND decomp

```sh
patches/exp/C1-combo/build.sh
# full C1 (what the 10M row used):
E2_LOGIT=1 BITL_LR_E6=3000 BITL_ACCUM=1 FX2_BIN=$PWD/build/exp/C1/bin/cmix \
  patches/fx2-cmix-transformer/slice_cmd.sh comp|decomp IN OUT
# or through slot + bench (sets those three variables, passes any other env through to both directions):
patches/exp/C1-combo/run.sh all enwik9_10M "notes"
E9_FXCM_FINAL=0 patches/exp/C1-combo/run.sh noE9 enwik9_1M "notes" --fast   # ablations
BITL=0          patches/exp/C1-combo/run.sh noE3 enwik9_10M "notes"
```

E1 gate3 is compiled in. `E2_LOGIT=0` turns E2 off, `BITL=0` turns E3 off and `E9_FXCM_FINAL=0` turns E9 off.
For a submission these would become compile-time constants.

## Results

Slice mode, `scripts/slot.sh scripts/bench.sh`, all round-trip verified (`cmp`). Ledger methods are `fx2T+C1-*`.
Timings are contended, so they are not speed claims.
The 10M C1-all row says `915a141+dirty`: the dirty file was other agents' uncommitted ledger rows. The C1 code
was unchanged since 0707157.

| Run | 1M bytes | Δ vs 126,473 | 10M bytes | Δ vs 1,264,682 |
|---|---:|---:|---:|---:|
| C1 all (E1+E2+E3+E9) | 126,240 | −233 | **1,262,015** | **−2,667 (−0.211%)** |
| C1 without E9 (`E9_FXCM_FINAL=0`) | 126,232 | −241 | | |
| C1 without E3 (`BITL=0`) = E1+E2+E9 | | | 1,262,339 | −2,343 (−0.185%) |
| sum of the parts measured alone | −258 (E1 −52, E2 −212, E3 −7, E9 +13) | | −2,853 (E1 −410, E2 −2,123, E3 −326, E9 +6) | |

- **10M: C1 keeps 93% of the sum.** −2,667 vs −2,859 for E1+E2+E3, about 190 B of overlap. Over the best
  single part (E2, −2,123), adding E1+E3+E9 gives a further −544. That is 74% of E1+E3's −736 alone.
- **E3 is fully additive.** Its marginal inside C1 is 1,262,339 → 1,262,015 = −324, against −326 alone. A
  post-SSE correction of the final p doesn't overlap with the mixer-side changes.
- **The whole overlap is between E1 and E2.** E1+E2+E9 gives −2,343, against −2,527 for the parts alone. On top
  of E2, E1 keeps about −220 of its −410. That is expected: both recalibrate how much the layer-0 mixers trust
  the transformer.
- Marginal value per scaled hour inside C1 (ESTIMATE): E2 ≈ −2,100 B for ≈1.0 h, E1 ≈ −220 B for ≈0.55 h,
  E3 ≈ −324 B for ≈1.4 h.
- E9 is neutral: +6 alone on 10M, +8 inside C1 on 1M.
- Peak RSS 5,811 MiB on 10M (record 5,798), so +13 MiB.

## Cost (ESTIMATE)

| Component | Dev box µs per coded byte | Source |
|---|---:|---|
| E1 gate3 | ≈2.7 | E1 README (code estimate) |
| E2 logit head | ≈5 (+ ≈0.5 for the second byte mixer, not in E2's transformer-only measurement) | E2 README (`perf stat`) |
| E3 agg16 | ≈6.9 | E3 README (micro-benchmark) |
| E9 | 0 | one load per bit |
| **Sum** | **≈15** | |

Cross-check, measured for C1: `perf stat -e instructions:u` on a 1M compress (552,288 coded bytes), record vs
C1 binary.

- Instructions: 1.1697e12 → 1.2538e12, i.e. +84.1e9 = **+152K instructions per coded byte (+7.2%)**.
- At the record's ≈10K instructions/µs in the coding loop (2.1M instructions per coded byte over 186.6 µs),
  that is ≈15 µs. It agrees with the sum.
- Cycles were contended and are not used.

Full enwik9: 586.46M coded bytes × 15 µs ≈ 8,800 s ≈ **2.4 h per direction on this box**. Scaled ×1.26 to the
record's 3700X timing, that is ≈ **3.1 h**, about **19 µs of the ≈48 µs per coded byte of headroom** (about 40%
of the ≈7.8 h). E3 is the most expensive part (≈1.4 h scaled) and the least valuable (−326 alone). Inside C1 it is still worth
−324 B (the `BITL=0` row), so it is additive but expensive.

RAM: E1 ≤26 MB worst case, E2 0.3 MB, E3 1.4 MB. Measured +13 MiB peak on 10M; it fits the ≈0.3 GB headroom.
Executable: +45,088 B unpacked. After UPX it would add roughly +15–20 KB to S (ESTIMATE, not packed).

Determinism: the build guards pass, and every row round-tripped. E2 and E3 keep their fixed-order reductions.

## License

E3's `bit_head.cpp` is ported from cmix-neif (GPL-3.0-only). See the E3 README. The combined work could be
distributed only under GPL-3.0 exactly.

## Next

- Confirm on enwik9_100M before relying on the slice gains at full scale.
- If time is tight, drop E3 first (−324 B for ≈1.4 h), or recover its time by skipping confident bits.
- E1 and E2 overlap. A gated variant of E2's slot (E1-style confidence gating applied to the E2 head's
  stretch(p)) might recover part of the lost ≈190 B.
