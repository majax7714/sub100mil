#!/usr/bin/env bash
# full_cmd.sh comp|decomp in out: patches/exp/C3-full/full_cmd.sh (see its header) with C4's binaries:
#   comp   build/exp/C4/bin/comp9_costlog -e enwik9 under env -i (only FX2_COSTLOG passes), then archive9's
#          decompressor is swapped for bin/cmix_packed, the submission's packed executable (what the clean packed
#          comp9 writes, as long as the costlog twin is observation-only)
#   decomp that ./archive9 with no arguments under env -i
# COMP9 / CLEAN_EXE override the two binaries, as in C3-full's script.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; B="$REPO/build/exp/C4/bin"
export COMP9="${COMP9:-$B/comp9_costlog}" CLEAN_EXE="${CLEAN_EXE:-$B/cmix_packed}"
exec "$REPO/patches/exp/C3-full/full_cmd.sh" "$@"
