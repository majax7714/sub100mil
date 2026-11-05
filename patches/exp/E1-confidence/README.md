# E1: transformer confidence as a context

Type: **model addition**. Base: the record (fx2-cmix-transformer-v1 @83f2603 + `patches/fx2-cmix-transformer/*.patch`),
frozen transformer weights, no retraining. Date: 2026-11-05.

## Hypothesis

The transformer outputs a 205-way next-byte distribution, but downstream it is collapsed to one bit probability
(`ByteMixer` → `byte_mixer_output`, one stretched input to the 23 layer-0 mixers and one extra input to the final
mixer). Its *confidence* (top-1 probability, entropy, top-1/top-2 margin, whether its top-1 byte is still consistent with
the bits seen so far) is not used. Quantized confidence used as a context should let the mixers trust the transformer
more when it is sure and less when it isn't.

## Where the transformer output is consumed

- `Predictor::TransformerByteUpdate` (src/predictor.cpp): runs the transformer once per byte and passes the
  float16-rounded row to `byte_mixer_->SetProbs`.
- `ByteModel::Predict` (src/models/byte-model.cpp): per bit, p(bit=1) = mass of the upper half of the remaining byte range,
  and `ex` = argmax byte in the remaining range.
- `Predictor::Predict`: `byte_mixer_output` is input #N to the 23 layer-0 mixers, extra input to the final mixer,
  part of `auxiliary_context_` (mixer context), and an override when it is exactly 0/1.
- fxcm already uses `lstmex` (= `ex`) and `lstmpr` as a mixer-selector context and an input (fxcmv1.cpp:4673).
- After the final mixer: `SSE` (Shelwien's 2-stage SSE/mixer chain, contexts = previous byte, flags, partial byte).

## Confidence features (all computed in the patch, `-DE1CONF`)

Per byte (after the transformer step, over the 205 guarded probabilities, normalized by their sum):
- `b_top`: top-1 probability bucket (>0.95, >0.8, >0.5, else) = 4
- `b_h`: entropy in bits (<0.25, <1, <2.5, else) = 4 (205 `log2f` per byte; only built when used)
- `b_mg`: top-1 − top-2 margin (>0.9, >0.6, >0.25, else) = 4

Per bit (from `ByteModel::Predict` of the byte mixer, after each bit):
- `q`: P(top byte | bits so far) = max prob / remaining mass, 8 bins (0.3/0.5/0.7/0.85/0.95/0.99/0.998)
- `alive`: whether the byte-level top-1 is still the argmax of the remaining range (i.e. not refuted by the bits so far)
- `cb = 2q + alive` (16 values); `e1_ctx = 4cb + b_top` (64 values)

## Variants (compile-time defines; `build.sh <name> <defines>`)

| Variant | Defines | Mechanism | enwik9_1M | Δ vs 126,473 | enwik9_10M | Δ vs 1,264,682 |
|---|---|---|---|---|---|---|
| base | (none) | record, rebuilt in build/exp/E1 (binary identical to refs cmix) | 126,473 | 0 | | |
| mix1 | `E1_MIX1` | (a) final mixer weight set selected by `e1_ctx` (64) instead of a single set | 127,046 | **+573** | | |
| mx13 | `E1_MX13` | (a) layer-0 mixer mx13: context partial byte → `cb*256 + partial byte` | 126,479 | +6 | | |
| mixnew | `E1_MIXNEW` | (a) a 24th layer-0 mixer, context `cb*256 + partial byte`, lr 0.002 | 126,479 | +6 | | |
| apm1 | `E1_APM=1` | (b) extra interpolating APM after the SSE, ctx `e1_ctx × bpos`, rate 0.02, (p_sse+p_apm)/2 | 126,659 | **+186** | | |
| gate1 | `E1_GATE=1` | 16 extra layer-0 inputs: the transformer's stretch(p) copied into the input selected by `cb`, others 0 | 126,455 | −18 | | |
| gate2 | `E1_GATE=2` | gate1 + 4 inputs gated by `b_top` (20 extra) | 126,438 | −35 | | |
| **gate3** | `E1_GATE=3` | gate2 + 4 gated by entropy + 4 gated by margin (28 extra) | **126,421** | **−52 (−0.041%)** | GATE3_10M | GATE3_10M_D |
| gate4 | `E1_GATE=4` | 16 `cb`-gated + 64 `e1_ctx`-gated (80 extra) | 126,452 | −21 | | |

All rows round-trip verified (`cmp`) through `scripts/slot.sh scripts/bench.sh`; ledger method names `fx2T+E1-<variant>`.
Timings in the ledger are contended (other agents on the box) and are not speed claims.

## What we learned

- **Using confidence to *select weight sets* does not help on slices.** New mixer contexts start from zero weights and
  the record's mixers learn slowly (final mixer lr 3e-4), so fragmenting a weight set costs more than the context buys
  (mix1 +573). Adding the confidence to an existing layer-0 context or as an extra mixer is neutral (+6): the 23 mixers
  already have rich contexts, and fxcm already selects its mixer weights by the transformer's expected byte.
- **An extra APM/SSE stage hurts** (+186): the record's SSE chain is tuned, and a cold 50/50 blend adds noise.
  A learned blend (or a much slower-adapting APM) might do better; not tried within the time box.
- **Confidence-gated inputs work** (gate1..3): instead of choosing a weight set, give every mixer K extra copies of
  stretch(p_transformer), only the one for the current confidence bucket nonzero. Each mixer (in its own contexts)
  learns a per-bucket *correction* to the transformer's weight, starting from zero, so nothing is lost while it learns.
  This is a piecewise-constant nonlinear recalibration of the transformer's logit. The byte-level features (top-1,
  entropy, margin) add on top of the per-bit one (−18 → −35 → −52). Making the buckets a full product (gate4, 64) is
  worse: too sparse for 1M.

## Cost (best variant, gate3)

- **Ops per coded byte:** 28 extra inputs on 461 (+6.1%) in the 23 layer-0 mixers' dot product and update. Mixers are
  15.6% of the loop (29.1 µs per coded byte), so about **+1.7 µs**. Feature extraction: one pass over 205 floats for
  sum/top-2 plus 205 `log2f` for the entropy (about **+1 µs**), and per bit one division and a few compares. Total
  **about 2.5–3 µs per coded byte (ESTIMATE, from code; about 1.5% of 182 µs)**. Without the entropy features (gate2)
  about 1.3 µs.
- **Full enwik9 (ESTIMATE):** 586.46M coded bytes × 2.7 µs ≈ 1,600 s ≈ **0.45 h per direction on this box**, about
  **0.55 h** on the 3700X reference (×1.26), against the record's ~7.8 h of headroom.
- **RAM:** +28 floats per mixer context. Each layer-0 mixer is capped at 10,000 contexts, so at most
  23 × 10,000 × 28 × 4 B ≈ **26 MB** worst case (the record has about 300 MB of headroom). The confidence state itself
  is a few bytes.
- **Determinism:** no new vendor-dependent instructions (build guards pass: no RCPPS/RSQRTPS/AVX-512/libmvec);
  `log2f` comes from glibc like the record's existing `logf`/`expf` (same glibc caveat as the record).

## Reproduce

```
patches/exp/E1-confidence/build.sh gate3 -DE1CONF -DE1_GATE=3     # ~1 min (release cmix only, PGO+LTO)
patches/exp/E1-confidence/run.sh gate3 enwik9_1M "E1_GATE=3 ..."  # needs the record build (slicetool/dict/weights)
```

## Next (if pursued)

- Gate other inputs too (fxcm's final output, PPMD) by transformer confidence, or gate by bpos × confidence.
- Initialize new mixer contexts from a shared set instead of zero, then retry weight-set selection (mix1); this mostly
  matters on slices, but the full run also has many cold contexts.
- A slow, learned blend for the APM (b) instead of 50/50.
- The slice gain is from a cold start; the full-run gain could be smaller or larger. Confirm on enwik9_100M or full.
