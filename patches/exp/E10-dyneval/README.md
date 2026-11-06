# E10: dynamic evaluation of the frozen fx2-T transformer

Type: **model addition** (online adaptation of the transformer's own parameters). Base: fx2-cmix-transformer-v1
(the record), frozen int4 weights, no offline training. Date: 2026-11-05. Status: **best single mechanism so far on 10M (−2,697 B, −0.213%), but about 1.5× over its time budget at the
best setting; needs decay (see Verdict).**

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
bk12q4d (best):   E10=1 E10_SITES=19 E10_OPT=0 E10_LR=2e-3 E10_LRH=2e-3 E10_MLPBK=12 E10_QMIN=4 E10_DECAY=1e-5
bk6q4d (budget):  E10=1 E10_SITES=19 E10_OPT=0 E10_LR=2e-3 E10_LRH=2e-3 E10_MLPBK=6  E10_QMIN=4 E10_DECAY=1e-5
```

Reproduce:
```sh
T=build/exp/E10/tree; rsync -a --exclude .git --exclude 'models/*.tch' third_party/fx2-cmix-transformer-v1/ $T/
(cd $T && git init -q && for p in ../../../../patches/fx2-cmix-transformer/*.patch \
   ../../../../patches/exp/E2-correction-head/*.patch ../../../../patches/exp/E10-dyneval/*.patch; do git apply $p; done)
patches/exp/E10-dyneval/build.sh $T build/exp/E10/cmix_e10
P=patches/fx2-cmix-transformer B=$PWD/build/exp/E10/cmix_e10
E="E10=1 E10_SITES=19 E10_OPT=0 E10_LR=2e-3 E10_LRH=2e-3 E10_MLPBK=12 E10_QMIN=4 E10_DECAY=1e-5"
scripts/slot.sh --fast scripts/bench.sh --method fx2T+E10-bk12q4d --slice data/slices/enwik9_1M \
  --comp "env $E FX2_BIN=$B $P/slice_cmd.sh comp {in} {out}" --decomp "env $E FX2_BIN=$B $P/slice_cmd.sh decomp {in} {out}"
```

## Results

### Bench rows

Slice mode, round-trip verified, ledger method `fx2T+E10-*`, so every number below is a ledger row.
- Baselines: 1M 126,473; 10M 1,264,682. E2 logit alone: 1M 126,261 (−212); 10M 1,262,559 (−2,123).
- Timings are contended (the shared box), so ignore comp_s/decomp_s.
- Peak RSS equals the base: 5,673 MiB on 1M and 5,797 MiB on 10M.

| Variant | Config | 1M bytes | Δ 1M | 10M bytes | Δ 10M |
|---|---|---|---|---|---|
| bk12h | sites 19, lr 1e-3, MLPBK 12, all neurons (first, slow kernels) | 126,021 | −452 (−0.357%) | | |
| bk12h + E2 logit | | 125,916 | −557 | | |
| bk12q4 | lr 2e-3, MLPBK 12, QMIN 4, **no decay** | 126,043 | −430 (−0.340%) | 1,264,748 | **+66** |
| bk8q4 | MLPBK 8 | 126,132 | −341 | | |
| bk6q4 | MLPBK 6, **no decay** | 126,199 | −274 | 1,265,732 | **+1,050** |
| bk12q4 + E2 logit | no decay | | | 1,264,098 | −584 (E2 alone −2,123) |
| **bk12q4d** | bk12q4 + **decay 1e-5** | 126,039 | −434 (−0.343%) | **1,261,985** | **−2,697 (−0.213%)** |
| bk12q4d + E2 logit | | 125,936 | −537 | not run (10M cap) | |

Rows `915a141+dirty` / `a4c6e83+dirty` were dirty only because this README (tracked) was being edited. The code
was the committed patch.

### The drift and the fix

Without decay, the constant-rate SGD drifts.
- **Proxy:** on the 10M proxy (the transformer's own loss, cumulative, printed every 1M tokens), bk12q4's gain
  over the frozen model falls steadily: −0.40% at 1M tokens, −0.28% at 2M, then −0.25, −0.21, −0.18, and −0.165%
  at 5.76M.
- **Full model:** the CM absorbs what is left. The full-model 10M result is +66 B, and combined with E2 the drift
  costs +1,539 B against E2 alone.
- **The fix:** decaying every adapted parameter toward its frozen value (`θ ← 1 + (θ−1)(1−λ)` per step) keeps the
  gain. With λ = 1e-5 (a memory of about 100K tokens), the 10M proxy reaches −0.375% and the full model −2,697 B.

10M proxy (transformer loss, 5,764,509 tokens; frozen 1.227169):

| Config (all MLPBK 12, QMIN 4 unless noted) | loss | Δ |
|---|---|---|
| lr 2e-3, no decay | 1.225147 | −0.165% |
| lr 2e-3, decay 1e-4 / 3e-5 / **1e-5** / 3e-6 | 1.224818 / 1.223450 / **1.222568** / 1.222568 | −0.19 / −0.30 / **−0.375** / −0.375% |
| lr 1e-3 / 5e-4, decay 1e-5 | 1.223233 / 1.224252 | −0.32 / −0.24% |
| lr 5e-4, no decay | 1.222781 | −0.36% |
| MLPBK 6, decay 1e-5 | 1.223759 | −0.28% |
| bk12q4d + E2 logit: E2 head on the adapted transformer | 1.221728 | −0.44% (E2 alone: 1.223531, −0.30%) |

### What each piece buys (1M proxy, transformer loss; frozen 1.275603)

| Config | loss | Δ |
|---|---|---|
| branch gains, residual-only gradient (no MLP backward), Adam lr 2e-5 / SGD lr 1e-3 | 1.273992 / 1.273858 | −0.126 / −0.137% |
| same, Adam lr 1e-4 (too high) / lr −1e-5 (sign check) | 1.282419 / 1.281610 | +0.53 / +0.47% |
| branch gains, top 2 layers only (Adam lr 1e-5, MLPBK 2) | 1.275152 | −0.035% |
| **+ exact MLP backward in all 12 layers** (SGD lr 1e-3 / 2e-3) | 1.271231 / 1.271005 | −0.343 / −0.360% |
| + hidden-neuron gains (sites 19), lr 1e-3 / 2e-3 | 1.269613 / 1.269100 | −0.470 / **−0.510%** |
| same, QMIN 2 / 4 / 8 (mean kept neurons 98 / 73 / 40 of 148) | 1.269473 / 1.269477 / 1.270409 | −0.48 / −0.48 / −0.41% |
| QMIN 4, MLPBK 8 / 6 / 4 | 1.270519 / 1.271170 / 1.271990 | −0.40 / −0.35 / −0.28% |
| biases on branch outputs (any rate down to 1e-6) | 1.28–2.44 | diverge |
| low-rank residual branch r 4/8 (Adam 1e-5, 1e-4; SGD 1e-3) | 1.28–1.98 | diverge |
| int8 instead of int16 gradient in the dh pass | 1.269715 | gives back about 4% of the gain, saves 7% of instructions (not kept) |

Lessons:
- **Gradient quality through depth matters most.** The exact MLP Jacobian (2.6× the residual-only gain) and adapting
  all 12 layers (−0.035% for the top 2 vs −0.137% for all 12) dominate.
- **Additive parameters (biases, a low-rank branch) diverge** under the truncated gradient. Multiplicative gains near
  1 are stable.
- **SGD beats Adam-style normalization** here.

## Cost

**Method:**
- Alternating E10-off and E10-on runs of the transformer-only proxy on a 200K prefix (107,696 tokens), one at a
  time, pinned to one core.
- Cycle difference per token, with off/on alternated to cancel drift in box load. Off varied 60.4–63.6 G cycles
  in quiet stretches; the noise is about ±10% of the E10 overhead.
- Instruction counts are exact (on 1M).
- Parallel runs of my own were useless for timing: four 7 MB transformer hot sets thrash the shared 16 MiB L3, so
  even the gains-only variant showed +35% cycles.
- Reference machine: ×1.26 (the record's 3700X timing vs our slice-based dev-box estimate, as in E2), over 586.5M
  coded tokens.
- **ESTIMATES**, not full-run measurements.

| Variant | extra instr/token | extra cycles/token | dev box µs/token | ref µs/token | ref hours per direction |
|---|---|---|---|---|---|
| gains only (sites 3, no MLP backward) | 27K | ≈26K | ≈6 | ≈7 | ≈1.2 |
| bk6q4d | ≈150K | ≈70K | ≈16 | ≈20 | **≈3.3** |
| bk8q4(d) | ≈181K | ≈90K | ≈20 | ≈25 | ≈4.1 |
| **bk12q4d** | ≈266K | ≈125K | **≈28** | **≈35** | **≈5.7** |
| bk12q4d + E2 logit | ≈350K | | ≈34 | ≈43 | ≈7 |
| (E2 logit alone, same method) | 70K | ≈29K | ≈6 | ≈8 | ≈1.3 |

- **Code IPC.** The E10 code runs at IPC ≈2.2–2.4: instruction-bound, not memory-bound. Its cost is:
  - about 105K fixed instructions per token (U^T, the 24 gain sites, per-layer quantize/norm-backward);
  - plus about 2.2K per kept neuron per token. Each kept neuron is one int16 dot over its int4 down column and half
    of a two-row int16 madd axpy over its up row.
- **RAM:** about 0.25 MB of state (hidden gains and their caches 12×768 floats ×3, gain sites, weight-scale
  vectors). It reads the forward arenas and copies no weights. Bench peak RSS is unchanged.
- **L3:** no new streamed footprint (the arenas are already hot); the state is about 0.25 MB.
- **Executable:** 398,464 B unpacked vs E2's 382,080 (+16 KB), roughly +6 KB of S after UPX (ESTIMATE).

**Budget verdict:** bk12q4d (≈5.7 h) does **not** fit the ≈4 h E10 share of the headroom. bk6q4d (≈3.3 h) fits.
bk12q4d + E2 (≈7 h) would take almost all of the 7.8 h headroom.

## Overlap with E2

**They overlap strongly at the transformer level and partially in the full model.**

- **10M proxy:** E2's head on top of the adapted transformer gains only −0.07% more (1.222568 → 1.221728). On the
  frozen transformer it gains −0.30%.
- **1M full model:** E2 adds −103 B on top of E10d (126,039 → 125,936), about half of its standalone −212.
- **10M full model:** E10d+E2 was not run (the 4-run cap). By the 1M ratio, a guess is −3,200 to −3,500 B against
  E10d's −2,697 (ESTIMATE).

The cheaper of the two at equal gain is E2. Per hour of reference time:
- E2 logit: about −2,100 B per 1.3 h;
- bk12q4d: about −2,700 B per 5.7 h;
- bk6q4d: unknown on 10M. Its proxy is −0.28% vs −0.375%, so perhaps −1,800 to −2,000 B per 3.3 h (ESTIMATE).

## Verdict

**E10 works, and it is the strongest single transformer-side mechanism measured on 10M (−2,697 B, −0.213%, vs E2's
−2,123). But it is expensive per byte gained, and it only works with decay.**
- Dynamic evaluation of the frozen record transformer is real: −0.51% of the transformer's own loss on 1M at the
  best setting, −0.375% on 10M with decay.
- Without decay it drifts and *hurts* at 10M. Any longer run must tune the decay (and probably the rate) at 100M+.
- At the best setting it costs ≈5.7 h on the reference machine (ESTIMATE), over budget. The budget variant (bk6q4d)
  is untested on 10M.
- It substitutes for E2 much more than it adds to it.

## What to try next

1. **Orchestrator:** run bk6q4d (budget) and bk12q4d+E2 on 10M. Then tune the decay on the 100M transformer-only
   proxy (`proxy.sh` on enwik8, about 3–4 h). A decay that is right for 5.7M tokens may be too fast or too slow for
   586M.
2. **Cut the per-neuron cost** (the main term).
   - An int4-packed transposed copy of W_up (+1.2 MB, 2 lines per neuron instead of 6) and a `vpmaddubsw` axpy.
   - Skip the dxn pass in the lowest backprop layer.
   - Run the backward only every 2nd token, with the gradient of both tokens accumulated. Untested; it halves the
     cost.
3. **Better gradients below the attention branches.** Exact MLP backward was the biggest single win. A cheap
   current-position attention Jacobian is the natural next step:
   - the SWA value path through the self-attention weight;
   - the KDA output gate and o-proj (W_oᵀ, 37K MAC per layer).
4. **Schedule:** rate decay `lr/(1+t/τ)` combined with weight decay, and per-site rates (attention vs MLP gains).
5. **Share the backward with E2.** E2's FW head and E10 both backprop the softcap/unembedding. If both are kept,
   compute dl/dxn once.
