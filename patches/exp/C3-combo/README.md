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

## Results

See the ledger rows `fx2T+C3-*`, and the C2 control on the X2 proxy (`fx2T+C2-v1` on `x2proxy_reord_r130000.xml`).
