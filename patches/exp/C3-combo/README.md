# C3: C2 + X2 metadata time law

Type: **combination**. Base: fx2-cmix-transformer-v1 (the record), frozen weights. Date: 2026-11-06.

C3 = C2 (`patches/exp/C2-combo`: C1 + E2 logit LR ×0.5 + E12 at lr 0.004) + X2's metadata-block time law and
redirect page-id law (`patches/exp/X2-timelaw`), a second final stage after E12's. C2 is kept as its own series so
its 100M number stays a clean C2 measurement; X2 is not re-run on 100M (the 100M slice has little metadata).

## Files

- `0001-C3-predictor-X2.patch`: X2's three `src/predictor.cpp` additions placed next to E12's lines in C2's
  predictor (X2's own patch has E12-alone context there). X2's `predictor.h` hunks apply unchanged.
- `build.sh`: C2's build + X2 (steps 9–10); output `build/exp/C3/bin/cmix`. PGO training uses C3's env.
- `run.sh <name> <slice-name|path> <notes> [--fast]`: C2's env plus `X2_DICT` (method `fx2T+C3-<name>`).

## Results (all round-trip byte-exact; ledger rows `fx2T+C3-v1`, control `fx2T+C2-v1`)

| slice | C2 | C3 | X2 increment | X2 alone on the record |
|---|---:|---:|---:|---:|
| `build/exp/X2/slices/x2proxy_reord_r130000.xml` (989,764 B) | 25,219 | **24,175** | −1,044 | −1,046 (25,268 → 24,222) |
| enwik9_10M | 1,261,828 | **1,261,700** | −128 | — |
| enwik9_1M | — | 126,244 (record 126,473) | | |

The X2 increment inside C3 reproduces X2's own number, so the two final stages stack additively.
