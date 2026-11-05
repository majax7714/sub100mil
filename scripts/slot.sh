#!/usr/bin/env bash
# Run a command while holding one of N "heavy run" slots, so at most N memory-hungry runs execute at once.
# The record's compressor uses about 6 GB on slices, and this box has 30 GiB. Default N=3.
# Usage: scripts/slot.sh <command...>     (e.g. scripts/slot.sh scripts/bench.sh --method ...)
# Timing measured under concurrent slots is CONTENDED (shared 16 MiB L3). Don't use it for speed claims.
set -u
LOCKDIR="${SLOT_DIR:-$(cd "$(dirname "$0")/.." && pwd)/build/locks}"
N="${SLOTS:-3}"
mkdir -p "$LOCKDIR"
while :; do
  for i in $(seq 1 "$N"); do
    exec {fd}>"$LOCKDIR/slot$i"
    if flock -n "$fd"; then
      echo "[slot] holding slot $i/$N" >&2
      "$@"; rc=$?
      exec {fd}>&-
      exit "$rc"
    fi
    exec {fd}>&-
  done
  sleep 10
done
