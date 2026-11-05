#!/usr/bin/env bash
# Run a command while holding a "heavy run" slot, so only a few memory-hungry runs execute at once.
# The record's compressor uses about 6 GB even on small slices, and this box has 30 GiB.
#
# Usage:
#   scripts/slot.sh <command...>          main lane: SLOTS slots (default 2), for 10M+ runs
#   scripts/slot.sh --fast <command...>   fast lane: FAST_SLOTS slots (default 1), ONLY for runs on
#                                         slices of 1 MB or less, so short runs never queue behind
#                                         hour-long ones
# 2 main + 1 fast = 3 runs of about 6 GB each at most, which leaves room for the desktop.
# Timing measured under concurrent slots is CONTENDED (shared 16 MiB L3). Don't use it for speed claims.
set -u
LOCKDIR="${SLOT_DIR:-$(cd "$(dirname "$0")/.." && pwd)/build/locks}"
PREFIX=slot N="${SLOTS:-2}"
if [[ "${1-}" == --fast ]]; then
  shift; PREFIX=fast N="${FAST_SLOTS:-1}"
fi
mkdir -p "$LOCKDIR"
while :; do
  for i in $(seq 1 "$N"); do
    exec {fd}>"$LOCKDIR/$PREFIX$i"
    if flock -n "$fd"; then
      echo "[slot] holding $PREFIX slot $i/$N" >&2
      "$@"; rc=$?
      exec {fd}>&-
      exit "$rc"
    fi
    exec {fd}>&-
  done
  sleep 10
done
