# E9: feed fxcm's real final prediction to layer 1 and `auxiliary_context_`

Type: **tuning** (bug fix). Base: the record (fx2-cmix-transformer-v1 @83f2603 + `patches/fx2-cmix-transformer/*.patch`).
Date: 2026-11-05. **Verdict: neutral. It is not worth keeping for compression, and it costs nothing to carry.**

## The bug

`Predictor::Predict` takes `fxcm_model_index = input_index - 1`, the last of fxcm's 431 outputs (`num_models =
439+1-2-7`), as "fxcm's final prediction". Two places use it:

- layer 1 (the final mixer) gets it as a stretched input (column `mixer_0_.size()`);
- `auxiliary_context_` = 15 × (p_fxcm + p_bytemixer)/2 selects the weight set of one layer-0 mixer.

But fxcm fills only 429 outputs per bit. Its last `AddPrediction(pr)` (the APM average) lands in output 428, so
slot 430 is the constant 0.5. That was found by A1's per-byte cost tool; see docs/design/A1-retrieval.md §1 and
`patches/tools/per-byte-cost/README.md`. As a result:

- the final mixer's "fxcm" column is always stretch(0.5) = 0, i.e. dead;
- `auxiliary_context_` only ever takes the values 3..11, driven by the transformer alone.

The real fxcm final still reaches the 23 layer-0 mixers as an ordinary input.

## Fix

`0001-E9-fxcm-final.patch`:

- `fxcmv1::ResetPredictions` records how many outputs fxcm filled for the current bit (`e9_last_filled`).
- `FXCM::FinalIndex()` returns `e9_last_filled - 1`. That is fxcm's last filled output, the same signal the
  per-byte cost tool logs as `cost_fx`, rather than a hard-coded 428.
- `fxcm_model_index = first fxcm input + FinalIndex()`.

Runtime switch: `E9_FXCM_FINAL=0` restores the record's behaviour. The fix is on by default and needs no env.
Comp and decomp must agree. Executable size is unchanged at 369,792 B unpacked. Run time is unchanged too
(one load per bit).

`build.sh` builds `build/exp/E9/tree`, then `build/exp/E9/bin/cmix`. It takes about 1 min and uses the record's
pipeline: g++ 15 PGO+LTO, `-march=x86-64-v3`, plus the AVX-512/EVEX, RCPPS/RSQRTPS and libmvec guards, all of
which pass. `run.sh <slice> <notes> [--fast]` runs it through slot + bench.

## Results

Slice mode, round-trip verified. Ledger method `fx2T+E9`. Timings are contended.

| Slice | Record | E9 | Δ |
|---|---:|---:|---:|
| enwik9_1M | 126,473 | 126,486 | +13 (+0.010%) |
| enwik9_10M | 1,264,682 | 1,264,688 | **+6 (+0.000%)** |

Inside the C1 combination on 1M (patches/exp/C1-combo): with E9 126,240 B, without it (`E9_FXCM_FINAL=0`)
126,232 B, so +8.

## Why it doesn't help

- fxcm's final was never missing. It is one of the 23 layer-0 mixers' 461 inputs, and they already weigh it per
  context. Exposing it to the final mixer adds a 26th column that mostly duplicates what the 23 mixer outputs
  already carry.
- In the final mixer it starts at weight 0 with lr 3e-4, so it costs a little while it learns.
- The `auxiliary_context_` change re-partitions one of 23 mixers' contexts, from 9 values to 16. That is
  neither better nor worse here.
- The record's mixer learning rates were tuned with the dead column, which may also play a part.

## What we'd try differently

Nothing worth a run. If the final mixer is ever retuned, re-check E9 with it. Or remove the dead column and the
two constant 0.5 inputs (slots 429/430) from the 23 layer-0 mixers as a tiny speedup: 2 of 461 inputs, about
0.1 µs per byte.
