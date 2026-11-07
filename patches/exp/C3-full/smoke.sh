#!/usr/bin/env bash
# smoke.sh DIR CODE_MIN DEC_MIN: de-risk a full run without running it (full mode is enwik9-only, there is no smaller
# full-mode input). Hold a main-lane slot around it: scripts/slot.sh patches/exp/C3-full/smoke.sh ...
#  1. comp9_costlog -e enwik9 in DIR/comp_run (env -i, FX2_COSTLOG=DIR/cost.bin) until CODE_MIN minutes after the
#     coder starts (progress.log appears), logging RSS / progress / cost-log size each minute to DIR/smoke.log, then kill.
#  2. A submission-layout archive9: clean cmix + .dict.comp + .tfweights + payload + header, run (env -i) in DIR/dec.
#     LIMIT: the Encoder keeps its whole output in memory until Flush (Encoder::out_), so a killed -e leaves an EMPTY
#     payload and no partial decode is possible. Step 2 therefore only checks the archive layout, the clean binary's
#     dictionary self-extraction + VerifyDictionary ("Running cmix decompression..."), and E12/X2 construction on
#     ./.dict; it then fails on the empty payload ("vocabulary of 256 bytes"), which is expected. The prefix cmp
#     below reports MISMATCH for the same reason.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"; B="$REPO/build/exp/C3-full/bin"
D="$(realpath -m "$1")"; CM=${2:-15}; DM=${3:-10}; L="$D/smoke.log"
rm -rf "$D"; mkdir -p "$D/comp_run" "$D/dec"
log() { echo "$(date +%T) $*" | tee -a "$L"; }
watch_pid() {  # watch_pid PID DIR PROGRESSFILE MINUTES
  local pid=$1 dir=$2 prog=$3 mins=$4 t0="" n=0
  while kill -0 "$pid" 2>/dev/null; do
    local c; c=$(pgrep -P "$pid" | head -1); local rss; rss=$(awk '/VmRSS/{print $2}' /proc/"$pid"/status 2>/dev/null)
    local crss=""; [[ -n "$c" ]] && crss=$(awk '/VmRSS/{print $2}' /proc/"$c"/status 2>/dev/null)
    local pr=""; [[ -f "$dir/$prog" ]] && pr=$(tail -1 "$dir/$prog" 2>/dev/null | tr '\r' ' ' | tail -c 80)
    local cl=""; [[ -f "$D/cost.bin" ]] && cl=$(stat -c %s "$D/cost.bin")
    log "pid $pid rss ${rss:-?} kB child ${crss:-none} kB | progress: ${pr:-none} | cost.bin ${cl:-none} B | $(ls -A "$dir" | tr '\n' ' ' | tail -c 200)"
    if [[ -z "$t0" && -s "$dir/$prog" ]]; then t0=$n; log "coder running"; fi
    if [[ -n "$t0" && $((n - t0)) -ge $mins ]]; then log "killing after $mins min of coding"; kill "$pid"; sleep 5; kill -9 "$pid" 2>/dev/null; break; fi
    sleep 60; n=$((n + 1))
  done
  wait "$pid" 2>/dev/null; log "process ended (rc $?)"
}
cd "$D/comp_run"; cp "$B/comp9_costlog" cmix
env -i PATH=/usr/bin:/bin HOME="$HOME" FX2_COSTLOG="$D/cost.bin" ./cmix -e "$REPO/data/raw/enwik9" payload \
  > "$D/comp.stdout" 2> "$D/comp.stderr" &
watch_pid $! "$D/comp_run" progress.log "$CM"
log "comp stderr:"; tr '\r' '\n' < "$D/comp.stderr" | tail -15 | tee -a "$L"
ls -la "$D/comp_run" | tee -a "$L"
# 2. decode the partial payload with the clean binary in the submission layout
P="$D/comp_run/payload"; cd "$D/dec"
cp "$B/cmix" hx; ./hx -h "$(stat -c %s ../comp_run/.dict.comp)" 0 "$(stat -c %s "$P")" "$(stat -c %s ../comp_run/.tfweights)" >/dev/null
cat "$B/cmix" ../comp_run/.dict.comp ../comp_run/.tfweights "$P" header.dat > archive9; chmod +x archive9; rm -f hx header.dat
env -i PATH=/usr/bin:/bin HOME="$HOME" ./archive9 > "$D/dec.stdout" 2> "$D/dec.stderr" &
# the decoder has no progress.log; its temp output appearing marks the coder start
watch_pid $! "$D/dec" .input_decomp.cmix.temp "$DM"
E="$D/comp_run/payload.cmix.temp"; Q="$D/dec/.input_decomp.cmix.temp"
n=$(stat -c %s "$Q"); n=$((n > 65536 ? n - 65536 : 0))
log "decoded prefix $(stat -c %s "$Q") B (comparing first $n B against the coded stream, $(stat -c %s "$E") B)"
if cmp -n "$n" "$E" "$Q"; then log "SMOKE PREFIX MATCH: $n B"; else log "SMOKE PREFIX MISMATCH"; fi
log "dec stderr:"; tr '\r' '\n' < "$D/dec.stderr" | tail -10 | tee -a "$L"
rm -f "$D/comp_run/ppm.temp" "$D/dec/ppm.temp"
