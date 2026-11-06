# E10: dynamic evaluation of the frozen fx2-T transformer

Type: **model addition** (online adaptation of the transformer's own parameters). Base: fx2-cmix-transformer-v1
(the record), frozen int4 weights, no offline training. Date: 2026-11-05. Status: see Verdict.

## Step 0: what the record already does online

**Nothing on the transformer side.** The record's transformer is pure inference:

- `cpp_infer/` has no backward pass, gradient or optimizer code. `TransformerOpt::step()` only runs the forward.
- The weights are fixed int4 arenas built once at load.
- The "pretraining" in the bench output is cmix's `preprocessor::Pretrain(&p, dictionary)`. It runs the CM models
  (bracket, fxcm, direct, match, indirect) over `english.dic` before coding. `src/runner.cpp` says so: "neither the
  ppmd nor the transformer is pretrained".
- The writeup confirms that fx2-cmix's online-trained LSTM was *replaced* by the offline-trained, frozen transformer.
- The only state that adapts within an article is the transformer's context: KV rings (window 1024) and KDA state.
  It is reset at every article piece.

So E10 is genuine dynamic evaluation, not "better online adaptation" of an existing learner.

Two facts shaped the design:
- **The final RMS norm has no learnable gain**, and the unembedding reads a quantized `xn`.
  - Adapting the output projection is therefore the same function class as E2's logit head.
  - To change hidden states, E10 has to adapt *internal* parameters.
- **There are no biases anywhere** in the model.

## Mechanism

Zero-init deltas on top of the frozen weights. They are adapted by one SGD step per coded token, on the main
transformer's own cross-entropy at the token just coded, using only already-coded symbols. Compressor and decompressor
replay this identically, and nothing goes into the archive. Unlike E2, E10 changes the transformer's main distribution
itself. That distribution reaches the CM through the existing byte-mixer slot; there is no new mixer input.

Adapted parameters (runtime selectable by `E10_SITES` bits):

| bit | parameters | count | forward |
|---|---|---|---|
| 1 | per-channel gain on each attention/KDA branch output (the o-proj output), all 12 layers | 12×192 | `x += g_a ⊙ y_attn` |
| 2 | per-channel gain on each MLP branch output (the down-proj output), all 12 layers | 12×192 | `x += g_m ⊙ y_mlp` |
| 16 | per-neuron gain on the MLP hidden `relu²` activation, in the backprop layers | ≤12×768 | `h = g_h ⊙ relu(y_up)²` before the u8 quantization (inside the `relu2q` epilogue) |
| 4, 8 | biases on the branch outputs | 12×192 each | diverge, don't use (see below) |
| 32 | low-rank residual branch `x += B(A·xn_mlp)` per layer (A fixed-LCG init, B zero init) | 12×r×384 | diverges, don't use |

Gains are per-row rescalings of the o-proj, down-proj and up-proj matrices, i.e. dynamic evaluation of their
quantization scales. That is the closest this model has to "LayerNorm gain" adaptation.

**Gradient (truncated, current-position-only backprop):**
1. CE at the main head → softcap derivative.
2. Back through the frozen unembedding (E2's float copy) → final RMS-norm backward.
3. Down the residual stream through all 12 layers. The residual coefficients and the U-net skip connections are exact.
4. The **MLP Jacobian is exact for the top `E10_MLPBK` layers**:
   - straight-through estimator (STE) through the int8/u8 activation quantizers;
   - `relu²'` from the exported pre-activation;
   - the hidden-gain chain rule;
   - RMS-norm backward of the MLP's input norm.
5. **Attention/KDA Jacobians are dropped.** Past positions' K/V are constants, and the attention branch is treated as
   constant w.r.t. its input.
6. The MLP backward only uses neurons whose `relu²` code is ≥ `E10_QMIN`. Small activations have small `relu²'`.

**Implementation:**
- The MLP backward reads the **forward arenas**: int4 down columns (2 cache lines) and int8 up rows from the dense G8
  arena. There are no weight copies.
- Exact integer dot products with int16-quantized gradient vectors (`vpmaddwd`). Integer sums are order-free, so they
  are deterministic.
- Software prefetch 8 neurons ahead.

Determinism:
- Plain float code in the transformer objects (`-O3`, no fast-math).
- Every float reduction has a fixed order. Integer dots are exact.
- Quantizers use `cvtps` (round-to-nearest-even under the default MXCSR), no RCPPS/RSQRTPS (build.sh re-runs the
  record's guards), no threads.
- Every bench row below round-tripped byte-exactly.
- With `E10=0` the output is bit-identical to the record (proxy loss 1.275603 = record).

## Files

- `0001-e10-dyneval.patch`: applies on top of `patches/fx2-cmix-transformer/*.patch` **and**
  `patches/exp/E2-correction-head/*.patch`, so one binary runs E10, E2 and E10+E2. It touches
  `cpp_infer/src/opt/{model_opt.h,model_opt.cpp,arena_build.h,arena_build.cpp,qmat_dense.h,qmat_dense.cpp}` and
  `src/predictor.{h,cpp}`.
- `build.sh [TREE] [OUT_BIN]`: E2's pipeline (g++ 15, `-march=x86-64-v3`, PGO+LTO, the AVX-512/EVEX and
  RCPPS/RSQRTPS/libmvec guards), release `cmix` only, about 1.5 min.
- `proxy.sh NAME SLICE.pre [ENV...]`: transformer-only run that prints the transformer loss (nats/token). E10 changes
  this loss directly, so it is the iteration proxy. About 1.2 GB RSS.
- `perfproxy.sh NAME CPU SLICE.pre [ENV...]`: the same under `perf stat`, pinned.

**Runtime switches** (env; comp and decomp must match; a submission would hard-code them):

| var | default | meaning |
|---|---|---|
| `E10` | 0 | 1 = on |
| `E10_SITES` | 3 | bitmask above; **use 19** (= 1+2+16) |
| `E10_MLPBK` | 0 | exact MLP backward in the top N layers (0–12) |
| `E10_QMIN` | 1 | MLP backward only through `relu²` codes ≥ QMIN |
| `E10_OPT` | 1 | 0 = SGD, 1 = Adam-style RMS normalization (`E10_BETA`, `E10_EPS`); **use 0** |
| `E10_LR` / `E10_LRH` / `E10_LRB` / `E10_LRL` | | rates for branch gains / hidden gains / biases / low-rank |
| `E10_LMIN` | 0 | adapt layers ≥ LMIN only |
| `E10_DECAY` | 0 | per-step decay toward the frozen value |
| `E10_RANK`, `E10_AINIT` | 4, 0.07 | low-rank branch rank and A init |
| E2's `E2_*` | | unchanged (see patches/exp/E2-correction-head/README.md) |

Recommended configurations:
```
bk12q4 (best):    E10=1 E10_SITES=19 E10_OPT=0 E10_LR=2e-3 E10_LRH=2e-3 E10_MLPBK=12 E10_QMIN=4
bk6q4 (budget):   E10=1 E10_SITES=19 E10_OPT=0 E10_LR=2e-3 E10_LRH=2e-3 E10_MLPBK=6  E10_QMIN=4
```

Reproduce:
```sh
T=build/exp/E10/tree; rsync -a --exclude .git --exclude 'models/*.tch' third_party/fx2-cmix-transformer-v1/ $T/
(cd $T && git init -q && for p in ../../../../patches/fx2-cmix-transformer/*.patch \
   ../../../../patches/exp/E2-correction-head/*.patch ../../../../patches/exp/E10-dyneval/*.patch; do git apply $p; done)
patches/exp/E10-dyneval/build.sh $T build/exp/E10/cmix_e10
P=patches/fx2-cmix-transformer B=$PWD/build/exp/E10/cmix_e10
E="E10=1 E10_SITES=19 E10_OPT=0 E10_LR=2e-3 E10_LRH=2e-3 E10_MLPBK=12 E10_QMIN=4"
scripts/slot.sh --fast scripts/bench.sh --method fx2T+E10-bk12q4 --slice data/slices/enwik9_1M \
  --comp "env $E FX2_BIN=$B $P/slice_cmd.sh comp {in} {out}" --decomp "env $E FX2_BIN=$B $P/slice_cmd.sh decomp {in} {out}"
```

RESULTS_PLACEHOLDER
