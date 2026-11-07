# C2: C1 + E2 logit LR ×0.5 + E12 arithmetic specialist

Type: **combination**. Base: fx2-cmix-transformer-v1 (the record), frozen weights. Date: 2026-11-06.

C2 = C1 (E1 gate3 + E2 logit head + E3 agg16 BITLSTM + E9 fxcm-final fix, see `patches/exp/C1-combo/README.md`)
with two additions:

- **E2 logit-head LR ×0.5** (`E2_A0=0.25 E2_AF=0.05`): A3's best fixed multiplier, already measured on C1 as
  ledger row `fx2T+C1-a3-lr05` (10M 1,261,970). Runtime only.
- **E12 arithmetic-consistency specialist** (`patches/exp/E12-arith`) with mixer lr from `E12_LR` (run.sh default
  0.004). E12 is a final stage after the record's SSE and E1's APM and before the transformer override, exactly as
  in E12 alone; it does not interact with C1's mixer inputs.

## Files

- `0001-C2-predictor-E12.patch`: E12's `src/predictor.cpp` hunks rebased onto C1's predictor (the only file where
  E12's own patch conflicts with C1). Everything else of E12 (PredictorOptions field, member, include, runner hook,
  `arith.h`) applies unchanged.
- `build.sh`: C1's build plus steps 7–8 (E12); output `build/exp/C2/bin/cmix`. PGO training runs with C2's env.
- `run.sh <name> <slice-name> <notes> [--fast]`: slot-limited bench in slice mode with C2's runtime env
  (method `fx2T+C2-<name>`). Comp and decomp get the same env.

```sh
patches/exp/C2-combo/build.sh
patches/exp/C2-combo/run.sh v1 enwik9_10M "first C2 run"
BENCH_TMP=$PWD/build/exp/C2/benchtmp patches/exp/C2-combo/run.sh v1 enwik9_100M "100M check"
```

## Results (all round-trip byte-exact; ledger rows `fx2T+C2-v1`)

| slice | record | reference | C2 |
|---|---:|---:|---:|
| enwik9_10M | 1,264,682 | C1 lr05 1,261,970 | **1,261,828** (−142 vs C1 lr05, −2,854 vs record) |
| enwik9_mid10M_pa | 337,982 | E12 lr0.004 324,627 | **323,802** (−14,180 vs record, −4.20%) |
| enwik9_100M | 12,006,323 | E2 alone 11,988,349 | **11,980,914** (−25,409 vs record, −0.212%) |

100M: comp 18,513 s, decomp 12,207 s (contended main lane), peak RSS 6,577.5 MiB (E2-alone 100M row 6,557.2).
Full-enwik9 cost on the reference machine [ESTIMATE]: record 37.2 h + C1's ≈3.1 h + E12 ≈0.2 h ≈ 40.5 h per
direction (target 45 h); RAM +≈20 MiB over the record (C1 +13 MiB, E12 a few MB) on the record's ≈9.66 GB.
