# E2: online correction head on the frozen fx2-T transformer

Type: **model addition**. Base: fx2-cmix-transformer-v1 (the record), frozen weights, no offline training.
Date: 2026-11-05. Status: **beats the baseline on 1M and 10M; worth more (see Verdict)**.

## Mechanism

A second output head beside the frozen transformer's head. It is trained *online*, one SGD step per predicted
token, on its own cross-entropy, from symbols already coded. Compressor and decompressor reconstruct it identically.
Nothing is stored in the archive. The static head, its distribution and everything downstream of it stay untouched.
The head's 205-way distribution goes through the same float16 round trip and 1e-6 floor as the static row. A second
`ByteMixer` byte slot turns it into a bit prediction, which becomes one extra input of the 23 layer-0 mixers and one
extra input of the final mixer (ex-girlfriend's FW1 wiring). At article-piece starts, where the transformer is not
stepped, the slot carries the PPMd row and no update happens (ex-girlfriend's `fw_live` pairing).

Two learners (both selectable at runtime, so one binary serves every variant):

- **(c) FW, ex-girlfriend "A-lite" ported to fx2-T** (`E2_FWK=k`): the quantized ReLU² codes `h_b` (768-d, sparse) of
  the last `k` MLP blocks drive a residual correction `dx = Σ_b dW_b h_b`, with `dW_b` 192×768 fp32, zero-initialized
  and never reset. The second head is `softmax(15 tanh(U Q(RMSNorm(x+dx))/15))` through the frozen unembedding. The
  update is ex-girlfriend's: the softcap/CE gradient goes back through a float copy of the unembedding and the
  RMS-norm backward. Per-feature RMS preconditioning (EMA 0.999) and an NLMS step
  `κ_t/(1+Σ h·sh)` with `κ_t = kf + (k0−kf)/(1+t/τ)` follow.
- **(a) logit head** (`E2_LOGIT=1`): an online softmax regression `A·[xn;1]` (205×193) on the final normalized hidden
  state, added to the pre-softcap logits of the second head. The update is NLMS with a constant denominator
  `1+D+1` (xn is RMS-normalized) and rate `a_t = af + (a0−af)/(1+t/τ_a)`.

Determinism: plain float code in the transformer objects (`-O3`, no fast-math, the same binary encodes and decodes).
All reductions have a fixed order: register-blocked FMA chains in index order, and a fixed 8-lane tree for the
feature-RMS mean. There is no RCPPS/RSQRTPS (build.sh re-runs upstream's guards) and no threads. Every bench row below
round-tripped byte-exactly.

## Files

- `0001-e2-correction-head.patch`: applies on top of `patches/fx2-cmix-transformer/*.patch`. It touches
  `cpp_infer/src/opt/{model_opt.h,model_opt.cpp,arena_build.h,arena_build.cpp}` and `src/predictor.{h,cpp}`.
- `build.sh [TREE] [OUT_BIN]`: the record's build pipeline (g++ 15, `-march=x86-64-v3`, PGO+LTO, AVX-512/EVEX and
  RCPPS/RSQRTPS/libmvec guards), producing only the release `cmix`. It takes about 1 min, because the comp9
  self-extraction step is skipped.
- Runtime switches (comp and decomp must match): `E2_FWK` (0–4), `E2_K0`, `E2_KF`, `E2_TAU`, `E2_LOGIT` (0/1),
  `E2_A0`, `E2_AF`, `E2_ATAU`, `E2_FINAL` (default 1). For a submission these would become compile-time constants.

Reproduce:
```sh
T=build/exp/E2/tree; rsync -a --exclude .git --exclude 'models/*.tch' third_party/fx2-cmix-transformer-v1/ $T/
(cd $T && git init -q && for p in ../../../../patches/fx2-cmix-transformer/*.patch ../../../../patches/exp/E2-correction-head/*.patch; do git apply $p; done)
patches/exp/E2-correction-head/build.sh $T build/exp/E2/cmix_e2
P=patches/fx2-cmix-transformer B=$PWD/build/exp/E2/cmix_e2
scripts/slot.sh scripts/bench.sh --method fx2T+E2-fw1lo --slice data/slices/enwik9_1M \
  --comp "env E2_FWK=1 E2_K0=2e-3 E2_KF=0.5e-3 FX2_BIN=$B $P/slice_cmd.sh comp {in} {out}" \
  --decomp "env E2_FWK=1 E2_K0=2e-3 E2_KF=0.5e-3 FX2_BIN=$B $P/slice_cmd.sh decomp {in} {out}"
```

## Results

Bench rows (slice mode, round-trip verified, ledger method `fx2T+E2-*`). Baselines: 1M 126,473 (reproduced exactly
by this copy's unmodified build) and 10M 1,264,682.

| Variant | Config | 1M bytes | Δ 1M | 10M bytes | Δ 10M |
|---|---|---|---|---|---|
| base | unmodified record build | 126,473 | 0 | | |
| fw4 | FW, last 4 blocks, ex-girlfriend rates κ 6e-3→1.5e-3, τ 8000 | 126,205 | −268 (−0.21%) | | |
| **fw4lo** | FW, last 4 blocks, κ 2e-3→0.5e-3 | **126,176** | **−297 (−0.235%)** | | |
| **fw1lo** | FW, last block only, κ 2e-3→0.5e-3 | 126,204 | −269 (−0.21%) | 1,262,517 | −2,165 (−0.171%) |
| **logit** | logit head, a 0.5→0.1, τ 8000 | 126,261 | −212 (−0.17%) | **1,262,559** | **−2,123 (−0.168%)** |
| fw1lo+logit | both learners in one second head | 126,211 | −262 | | |
| fw4vlo | FW4, κ 0.8e-3→0.2e-3 | cancelled (slot starvation; FW4 is unaffordable anyway) | | | |

**100M (enwik9_100M = enwik8), 2026-11-06, logit head** (same settings as its 10M row, `E2_LOGIT=1`, build from the
committed patch, `cmix_e2c`): **11,988,349 B (0.9591 bpb) vs the record's 12,006,323 B (0.9605 bpb) on the same
slice: −17,974 B (−0.150%)**. Both round-tripped. The relative gain shrinks slowly with length: −0.17% (1M),
−0.168% (10M), −0.150% (100M). Peak RSS 6,557 MiB for both (5,797 at 10M). Wall times (record 21,482 / 16,605 s,
logit 11,687 / 11,624 s) were taken under different contention and say nothing about E2's cost.

Proxy (`--transformer-only`; the second head's own loss, in nats/token, vs the static head; no mixers). This was used
to choose rates:

| Config | 1M head loss (static 1.275603) | 10M head loss (static 1.227169) |
|---|---|---|
| logit a 0.5→0.1 | 1.273030 (−0.20%) | 1.223531 (−0.30%) |
| logit a 2→0.4 / 8→1.5 / 0.15→0.03 | 1.280636 / 1.312900 / 1.272683 | |
| FW4 κ 6e-3→1.5e-3 (ex-girlfriend) | 1.283557 (+0.62%) | |
| FW4 κ 2e-3→0.5e-3 | 1.272720 (−0.23%) | 1.224312 (−0.23%) |
| FW4 κ 0.8e-3→0.2e-3 | 1.271301 (−0.34%) | 1.222207 (−0.40%) |
| FW1 κ 2e-3→0.5e-3 / 4e-3→1e-3 | 1.272924 / 1.277665 | |
| FW1 κ 2e-3 + logit, one head | 1.274830 | |

ex-girlfriend's rates (tuned on neif) are too high for fx2-T. The head alone is *worse* than the static head on 1M,
but the mixers still extract −268 B from it.

## Cost

Measured with `perf stat`, `--transformer-only` on 1M (552,288 tokens), the same binary with E2 off vs on, pinned to
one core. The box was shared with other agents' runs (L3 contention), so read these as ±1–2 µs.

| Variant | Extra instr/token | Extra time/token (dev box) | Full enwik9, 586.5M tokens, dev box | Scaled ×1.26 to the record's 3700X timing (ESTIMATE) | Extra RAM |
|---|---|---|---|---|---|
| logit | 71K | **≈5 µs** | ≈0.8 h | ≈1.0 h | A 160 KB + unembed copy 157 KB + slot ≈ **0.3 MB** |
| FW1 | 56K | **≈12.7 µs** | ≈2.1 h | ≈2.6 h | dW 576 KB + 157 KB + stats ≈ **0.8 MB** |
| FW4 | 144K | **≈47 µs** | ≈7.7 h | ≈9.7 h | dW 2.25 MiB + ≈0.2 MB ≈ **2.5 MB** |

- Bench peak RSS: base 5,671.9 MiB; logit 5,672.4; FW1 5,672.9 (+0.5 to +1 MiB, including the extra mixer weights).
  Against the record's 9.66 of 10 GB, all variants fit RAM.
- Executable: +12,288 B unpacked (369,792 → 382,080), roughly +5 KB of S after UPX (ESTIMATE).
- Time is what binds: the headroom is about 7.8 h at 45 h. FW4 does not fit. FW1 uses about a third of the headroom;
  logit about an eighth. The FW update is memory-bound (read-modify-write of the touched dW columns). FW1's 0.56 MiB
  fits Zen 4's 1 MiB L2 but not the 3700X's 512 KiB L2, so the reference-machine cost may be higher than the
  ×1.26 scaling suggests. In a full run the CM's DRAM traffic also competes for L3; not measured.
- An optimization pass already taken, bit-identical: reuse `qgemv_relu2q`'s nonzero list and keep the FW
  forward/backward accumulators in registers. FW1 went from 23 to 12.7 µs/token and FW4 from 63 to 47.

## Verdict

**Worth more, and keep the logit head as the lead candidate.** Every variant beats the record, but the gains are
modest.

- **The best mechanism by cost is (a), the logit head.** It gives −0.168% on 10M (−2,123 B), the same relative
  gain as on 1M. It costs about 5 µs/token (≈1 h at full scale, scaled to the reference machine; ESTIMATE) and
  0.3 MB of RAM.
- **FW1 gains slightly more on 10M (−2,165 B) at about 2.5× the time** (≈2.6 h). FW4 is the best on 1M (−297) but
  costs about 9.7 h and doesn't fit.
- **The online learners did not gain more over a longer input.** FW1 fell from −0.21% (1M) to −0.17% (10M); the
  logit head stayed at −0.17%. That differs from the proxy, where the head's own loss gain grew from 1M to 10M.
  As the CM warms up, it captures more of what the head corrects.
- **Full-enwik9 extrapolation (ESTIMATE, speculative):** −0.17% of the record's 96,996,198 B archive is about
  −165 KB for about 1 h. That is the same order as ex-girlfriend's −135 KB on neif. Slice caveats apply: the
  transformer's share of the bits is larger on a cold-CM slice, so the full-file gain is probably smaller.
- **100M done** (−0.150%, see Results); still needed: a full run before it goes into a combined
  build.

## What to try next

- **bf16/fp16 storage for dW** (deterministic conversions): halves the FW update's memory traffic and makes FW1
  L2-resident on Zen 2.
- **Rates tuned on longer slices**: the proxy keeps improving at lower κ (FW4 0.8e-3 beat 2e-3 on 10M head loss).
  Retune at 100M (transformer-only proxy runs at about 100 µs/token).
- **Two separate second heads** (FW1 and logit as two slots) instead of one shared head. In one head they interfere
  (−262 vs −269 for FW1 alone).
- An SSE/APM keyed by the disagreement between the static and corrected heads (ties into E1).
