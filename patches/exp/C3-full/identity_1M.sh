#!/usr/bin/env bash
# identity_1M.sh DIR: slice-mode 1M compress with the clean build and with the costlog twin (FX2_COSTLOG on), under
# env -i (binaries from BIN_DIR, default build/exp/C3-full/bin); the two archives must be byte-identical (costlog is observation-only). Run in the fast lane:
#   scripts/slot.sh --fast patches/exp/C3-full/identity_1M.sh build/exp/C3-full/identity
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; B="$(realpath "${BIN_DIR:-$REPO/build/exp/C3-full/bin}")"; S=patches/fx2-cmix-transformer/slice_cmd.sh
D="$(realpath -m "$1")"; rm -rf "$D"; mkdir -p "$D/clean" "$D/costlog"; cd "$REPO"
env -i PATH=/usr/bin:/bin HOME="$HOME" FX2_BIN="$B/cmix" $S comp data/slices/enwik9_1M "$D/clean/comp.bin"
env -i PATH=/usr/bin:/bin HOME="$HOME" FX2_BIN="$B/cmix_costlog" FX2_COSTLOG="$D/costlog/cost.bin" $S comp data/slices/enwik9_1M "$D/costlog/comp.bin"
ls -l "$D"/*/comp.bin "$D/costlog/cost.bin"
if cmp "$D/clean/comp.bin" "$D/costlog/comp.bin"; then echo "IDENTICAL $(stat -c %s "$D/clean/comp.bin") B"; else echo "DIFFERENT"; fi
python3 patches/tools/per-byte-cost/costlog.py "$D/costlog/cost.bin" | head -3
