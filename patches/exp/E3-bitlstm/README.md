# E3: neif's BITLSTM, online-only, on the record (fx2-cmix-transformer-v1)

**Type:** model addition. **Base:** the record, with frozen transformer weights and no retraining. **Date:** 2026-11-05.

**Verdict: worth more, not keep.** Online-only, the head gains almost nothing on short slices:

- 1M: −3 to −12 B, which is noise. In mode 1 it costs +4 B.
- 10M (agg16): 1,264,356 B, which is −326 B (−0.026%).
- Its per-MB gain does grow along the stream (see the learning curve below), so it is still learning at 10M.

Suppose the relative gain plateaus somewhere between the 0.035% measured at 5 MiB and the roughly 0.25% that neif's
online head implies. Then full enwik9 would gain about 30–250 KB (SPECULATIVE, unmeasured). At about 1.1–1.4 h of
full-run time, the mechanism isn't worth keeping in this form. neif's value came mostly from
**offline replay training**, which is CPU-only and allowed under the plan of record. That, not more online
variants, is the next step (see "What next").

## Hypothesis

neif's 16-cell BITLSTM corrects the final bit probability in logit space, just before the arithmetic coder.

- neif's README says it is worth about −0.3 MB on archive9 vs no BITLSTM (estimate) for about 3–4% of run time.
- Its trained weights beat an online-only 32-cell head by only about −44 KB of archive over the full run.
- So an online-only port, with deterministic init and no weights, should keep most of that gain on the record.

## What was built

Files: `0001-bitlstm-online.patch`, applied after `patches/fx2-cmix-transformer/*.patch`. `build.sh` sets up
`build/exp/E3/tree` and builds with the record's pipeline. `run.sh` wraps `slot.sh` and `bench.sh`.

- **`src/bit_head.{h,cpp}`** ports neif's `src/bit_head.cpp`. What it keeps from neif:
  - The form: x → in_proj → SiLU → LSTMCell(C) → linear(1) → delta, with |delta| ≤ 8 and p' = σ(t + delta).
  - The AVX2 Exp8/Sigmoid8/Tanh8 kernels.
  - The splitmix64 init: PyTorch-style uniform, forget-gate bias +1, output layer exactly 0, so the head starts
    as a no-op.
  - Training: Adam, buffered truncated BPTT over 1,024-bit windows with a state reset per window, a mean
    gradient, a global norm clip of 1.0, and skipping any step with a non-finite gradient.
  - The fixed ±1/√192 random 32-d sketch of the transformer state, recomputed once per byte.
- **What differs from neif:**
  - There is no weight blob; the head learns online from bit 0.
  - There is no byte-LSTM block (the record has no byte LSTM) and no SSE, zone or ISBN features.
  - Configuration is read at run time from `BITL_*` env (cells, lr, windows per step, mode, dropped
    stage-1 columns).
  - Two kernel rewrites, which change only the summation order:
    - The forward pass uses transposed weight copies, so there are no horizontal sums.
    - The backward pass stores dpre/dz and accumulates the weight gradients as register-tiled outer products
      after the BPTT sweep.

    neif's per-step accumulators did not fit in L1 at 32 cells (5× slower). The rewrite was checked on a
    synthetic learnable task, with the same loss curve as neif's backward to six digits.
- **Inputs (70 features + bias):**
  - all 25 final-mixer (stage-1) inputs: 23 layer-0 mixers, fxcm, transformer bit p;
  - the raw final-mixer logit, t/T, (t/T)², sgn t (T = 12.203);
  - a bit-position one-hot(8) and the previous bit;
  - a 32-d sketch of the transformer's normalised final hidden state `xn`. The patch adds a
    `TransformerOpt::last_hidden()` accessor; the transformer's numerics are unchanged.
- **Where it plugs in:**
  - **mode 0**, as in neif: in `Encoder::Encode`/`Decoder::Decode` after `Discretize(Predict())`, with
    `Observe(bit)` before `Perceive`. The rest of the predictor never sees the corrected p. Byte-mixer 0/1
    override bits are run forward but are neither corrected nor learned from.
  - **mode 1:** the head runs before the final mixer as a standalone recurrent predictor (t = m1raw = 0), and
    its logit becomes a 26th final-mixer input.
- **Determinism:**
  - One TU, `bit_head.o`, built with `-O3 -fno-math-errno -ffp-contract=off -mrecip=none`: no fast-math, no
    PGO, no LTO. Its entry points are noinline, and all reductions run in a fixed order.
  - The release binary passes the record's RCPPS/RSQRTPS/AVX-512/libmvec guards.
  - Every run below round-tripped byte-exactly.
  - Configuration comes from env, so compression and decompression must share it. A submission would hard-code
    the constants.

## Results

Slice mode, `scripts/slot.sh scripts/bench.sh`, decomp_size 3,601,651. All timings are **contended**: other agents
shared the L3, and 1M wall times ranged 219–445 s against 112 s for an idle baseline. So no speed claims come from
these runs.

| Variant (method `fx2T+E3-…`) | Config | 1M bytes | Δ vs 126,473 | 10M bytes | Δ vs 1,264,682 |
|---|---|---:|---:|---:|---:|
| base | unmodified record rebuilt in build/exp/E3 (binary identical to refs `cmix`) | 126,473 | 0 | | |
| fast16 | mode 0, 16 cells, lr 1e-3, 4 windows/step | 126,461 | −12 | | |
| agg16 | mode 0, 16 cells, lr 3e-3, 1 window/step | 126,466 | −7 | 1,264,356 | **−326** |
| agg32 | mode 0, 32 cells, lr 3e-3, 1 window/step | 126,470 | −3 | | |
| mix16 | mode 1 (final-mixer input), 16 cells, lr 3e-3, 1 window/step | 126,477 | +4 | | |

neif's literal online setting (lr 3e-4, 16 windows per step, so one Adam step every 16 K bits) was queued and then
cancelled to save slot time. fast16 already learns 4× faster than it and gains only 12 B.

**Learning curve, agg16 on 10M** (`BITL_STATS=1`; measurement only). It compares the code length of each bit under
the base and corrected p, per 1 MiB of coded stream:

| Coded stream position | Window gain | Cumulative gain | Base cost so far |
|---|---:|---:|---:|
| 0–1 MiB | 14 B | 14 B | 243,739 B |
| 1–2 MiB | 35 B | 48 B | 490,321 B |
| 2–3 MiB | 56 B | 104 B | 727,792 B |
| 3–4 MiB | 89 B | 194 B | 967,764 B |
| 4–5 MiB | 72 B | 266 B | 1,201,474 B |
| end (5.76 MB) | | 326 B (archive delta) | |

The window gain rises from 0.006% to about 0.035% of each MiB's cost, and it has not clearly saturated by 5 MiB.

## Mechanism and why it gains so little online

- The record's final p is already the output of 23 layer-0 mixers, a final mixer and an SSE stage, all
  learning online. Most of what a 3.3 K-parameter head can learn online from the same stage-1 row, it learns
  only slowly, and the SSE stage already captures it.
- The head's distinct assets are intra-byte recurrence and the transformer's hidden state. They pay off only
  after many optimizer steps; the window gain grows roughly linearly over the first several MiB.
- neif's −0.3 MB came from a head trained by **32 replay passes over 20% of the stream** (418 M bits), i.e.
  thousands of times more gradient steps than a 10M slice gives online.
- mode 1 is slightly worse. As one more input among 26 to a mixer that is already well tuned, a weak and noisy
  predictor costs the final mixer a little.
- 32 cells are no better than 16 at this horizon, and cost about 3× the time.

## Cost

**Ops per coded byte (16 cells):**

- Forward: about 3.2 K MAC per bit.
- Backward: about 5 K MAC per bit (da/dhn mat-vecs, outer products, in_proj).
- Plus 6 K MAC per byte for the hidden sketch.
- Total about 70 K MAC per coded byte, about 6.1 K instructions per bit.

**Microbenchmark of the head alone:** pinned, L1-hot, on the shared (contended) box.

| | 16 cells | 32 cells |
|---|---|---|
| Per bit | 0.86 µs | 2.2–2.6 µs |
| Per coded byte | **6.9 µs** | about 18–21 µs |
| Share of the record's 186 µs per coded byte | about 3.7% | |

This matches neif's "+3 to +4%". In the real run, cache pollution from the CM side will make it somewhat worse.

**Full enwik9 (ESTIMATE):**

| | 16 cells | 32 cells |
|---|---|---|
| Dev box | 586.46 M coded bytes × 6.9 µs ≈ **1.1 h** | about 3–3.5 h |
| Reference 3700X (scaled by the record's official/our per-byte ratio, 1.26) | **about 1.4 h** | about 4 h |
| Share of our 7.8 h headroom | about 18% | |

**RAM:**

- 16 cells: the TBPTT window buffers are 1,024 × (inw + 15C + 1) floats = 1.28 MB. Weights, Adam moments,
  gradients, transposed copies and the sketch matrix add about 0.1 MB. **About 1.4 MB extra.**
- 32 cells: about 2.5 MB.

Both are negligible against the 0.3 GB of headroom. Measured peak RSS on 10M was 5,797.9 MiB, against 5,797.5 for
the baseline. But the window buffer is streamed once per 1,024 bits through
the L3 the transformer is capacity-sensitive to: about 1.3 MB beside its 7.2 MB hot set. That is probably harmless
below the about 4 MiB co-tenant level measured in docs/refs/fx2-cmix-transformer.md §6.

**Possible speedups if it is ever kept:**

- Skip the head when |t| is large, as neif does for adaptive-depth bits (39% of bits).
- Use a shorter window or a smaller input row.

## License

The ported code (bit_head.cpp kernels, init, training loop) comes from cmix-neif-1.0, which is **GPL-3.0-only**. Its
AVX2 kernels and form come from cmix-obias `bitlstm32` (GPL-3.0), and Exp8 is from avx_mathfun (zlib).

- The record is GPL-3.0. Its LICENSE is the GPL v3 text; we did not find an "or later" grant.
- GPL-3.0-only code can be combined into a GPL-3.0 work, but the combined work can then be distributed only under
  GPL-3.0 exactly, with no "or later" option.
- Attribution must be kept as in the file header.

**Flagged, not resolved.**

## What next (if pursued)

1. **Offline replay training on our own trace** (CPU only, no dense GPU training):
   - Dump the head's 70 inputs per bit from one full compression run. At 4.7 G bits × 70 floats that is about
     1.3 TB, so dump a 10–20% prefix in bf16, or record only the stage-1 row + logits and recompute the sketch.
   - Replay with neif's schedule (32 passes, lr 3e-3 → 3e-5 cosine) and compile in 3.3 K bf16 weights
     (about 7 KB, counted twice in S).
   - neif says trained beats online by about 44 KB over the full run, and online already gives some gain at
     full scale.
2. Measure the online head on **enwik9_100M**, to see whether the per-MB gain keeps growing or saturates before
   paying for a full run.
3. If it is kept, recover its time first, e.g. through E4 (mixer pruning) or by skipping confident bits.
