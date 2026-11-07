#!/usr/bin/env bash
# Full-mode (enwik9 only) round-trip wrapper for scripts/bench.sh, the submission's own path:
#   full_cmd.sh comp   <enwik9> <out>   ./cmix -e enwik9 (cmix = COMP9, default bin/comp9_costlog), then
#                                       <out> = archive9 with its decompressor swapped for CLEAN_EXE (default
#                                       bin/cmix, the clean submission binary), i.e. exactly what the clean comp9
#                                       produces when the costlog twin is observation-only
#   full_cmd.sh decomp <archive9> <out> ./archive9 with no arguments (the submission's decompression), -> <out>
# Both run in a subdirectory of <out>'s directory (bench.sh's work dir; put BENCH_TMP on disk: ppm.temp is a
# 14 GiB sparse mmap, the -e intermediates about 4 GB) under `env -i`, so no environment variable reaches the
# binaries except FX2_COSTLOG (comp only, if set: the costlog twin's per-byte log; 40 B per coded byte, ~23 GB).
# Logs (stdout/stderr of each step, progress.log) are kept next to <out> as comp.*/decomp.*.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="$REPO/build/exp/C3-full/bin"
COMP9="${COMP9:-$B/comp9_costlog}"; CLEAN_EXE="${CLEAN_EXE:-$B/cmix}"
mode="$1"; in="$(realpath "$2")"; out="$(realpath -m "$3")"; W="$(dirname "$out")"
ENV=(env -i PATH=/usr/bin:/bin HOME="$HOME")
case "$mode" in
  comp)
    [[ -n "${FX2_COSTLOG-}" ]] && ENV+=(FX2_COSTLOG="$(realpath -m "$FX2_COSTLOG")")
    D="$W/comp_run"; rm -rf "$D"; mkdir -p "$D"; cd "$D"
    cp "$COMP9" cmix; chmod +x cmix
    "${ENV[@]}" ./cmix -e "$in" payload > "$W/comp.stdout" 2> "$W/comp.stderr"
    [[ -s archive9 && -s .decomp_bin ]] || { echo "full_cmd: -e produced no archive9" >&2; exit 1; }
    n=$(stat -c %s .decomp_bin)
    cmp -s -n "$n" .decomp_bin "$COMP9" || { echo "full_cmd: .decomp_bin is not COMP9's executable" >&2; exit 1; }
    { cat "$CLEAN_EXE"; tail -c +$((n + 1)) archive9; } > "$out"
    echo "archive9 (as produced, decompressor $n B): $(stat -c %s archive9) B; payload $(stat -c %s payload) B;" \
         "submission archive9 (clean decompressor $(stat -c %s "$CLEAN_EXE") B): $(stat -c %s "$out") B" > "$W/comp.sizes"
    cp -f progress.log "$W/comp.progress.log" 2>/dev/null || true
    cd "$W"; rm -rf "$D" ;;
  decomp)
    D="$W/decomp_run"; rm -rf "$D"; mkdir -p "$D"; cd "$D"
    cp "$in" archive9; chmod +x archive9
    "${ENV[@]}" ./archive9 > "$W/decomp.stdout" 2> "$W/decomp.stderr"
    mv enwik9_uncompressed "$out"
    cp -f progress.log "$W/decomp.progress.log" 2>/dev/null || true
    cd "$W"; rm -rf "$D" ;;
  *) echo "usage: full_cmd.sh comp|decomp in out" >&2; exit 2 ;;
esac
