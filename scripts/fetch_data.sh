#!/usr/bin/env bash
# Fetch enwik9, derive enwik8 and working slices, and verify checksums.
# Idempotent: re-running skips the download if the zip is already present and
# intact, rebuilds only missing/wrong-size outputs, and re-verifies everything.
#
# Layout (relative to repo root):
#   data/raw/enwik9.zip, data/raw/enwik9
#   data/slices/enwik9_1M, enwik9_10M, enwik9_100M (== enwik8), enwik9_mid10M,
#               enwik9_mid10M_pa, enwik9_mid1M_pa (page-aligned mid slices, see below)
#   data/slices/enwik8 -> enwik9_100M (symlink)
#   data/CHECKSUMS.sha256
#
# Usage: scripts/fetch_data.sh [--rehash]
#   --rehash  regenerate data/CHECKSUMS.sha256 instead of verifying against it
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="$REPO_ROOT/data"
RAW="$DATA/raw"
SLICES="$DATA/slices"
CHECKSUMS="$DATA/CHECKSUMS.sha256"

URLS=(
  "http://mattmahoney.net/dc/enwik9.zip"
  "https://mattmahoney.net/dc/enwik9.zip"
)
ZIP_SIZE=322592222          # Content-Length served by mattmahoney.net
ENWIK9_SIZE=1000000000
ENWIK9_MD5="e206c3450ac99950df65bf70ef61a12d"
ENWIK8_SIZE=100000000
ENWIK8_MD5="a1fa5ffddb56f4953e226637dabbb36a"

REHASH=0
[[ "${1:-}" == "--rehash" ]] && REHASH=1

log()  { printf '[fetch_data] %s\n' "$*"; }
die()  { printf '[fetch_data] ERROR: %s\n' "$*" >&2; exit 1; }
fsize() { stat -c %s "$1"; }

mkdir -p "$RAW" "$SLICES"

# --- 1. download -----------------------------------------------------------
zip_ok() {
  [[ -f "$RAW/enwik9.zip" ]] \
    && [[ "$(fsize "$RAW/enwik9.zip")" == "$ZIP_SIZE" ]] \
    && unzip -tqq "$RAW/enwik9.zip" >/dev/null 2>&1
}

if zip_ok; then
  log "enwik9.zip present and intact; skipping download"
else
  rm -f "$RAW/enwik9.zip" "$RAW/enwik9.zip.part"
  ok=0
  for url in "${URLS[@]}"; do
    log "downloading $url"
    if curl -fL --retry 3 --retry-delay 5 -o "$RAW/enwik9.zip.part" "$url"; then
      mv "$RAW/enwik9.zip.part" "$RAW/enwik9.zip"
      if zip_ok; then ok=1; break; fi
      log "downloaded zip failed integrity check; trying next URL"
      rm -f "$RAW/enwik9.zip"
    fi
  done
  [[ $ok == 1 ]] || die "could not download a valid enwik9.zip"
fi

# --- 2. unzip --------------------------------------------------------------
if [[ -f "$RAW/enwik9" && "$(fsize "$RAW/enwik9")" == "$ENWIK9_SIZE" ]]; then
  log "enwik9 present with correct size; skipping unzip"
else
  log "unzipping enwik9"
  rm -f "$RAW/enwik9"
  unzip -oq "$RAW/enwik9.zip" enwik9 -d "$RAW"
fi
[[ "$(fsize "$RAW/enwik9")" == "$ENWIK9_SIZE" ]] || die "enwik9 has wrong size"

# --- 3. slices -------------------------------------------------------------
# make_slice NAME SKIP_BYTES COUNT_BYTES
make_slice() {
  local out="$SLICES/$1" skip="$2" count="$3"
  if [[ -f "$out" && ! -L "$out" && "$(fsize "$out")" == "$count" ]]; then
    log "slice $1 present; skipping"
    return
  fi
  log "creating slice $1 (offset $skip, $count bytes)"
  rm -f "$out"
  dd if="$RAW/enwik9" of="$out.tmp" bs=1M iflag=skip_bytes,count_bytes \
     skip="$skip" count="$count" status=none
  [[ "$(fsize "$out.tmp")" == "$count" ]] || die "slice $1 wrong size"
  mv "$out.tmp" "$out"
}

make_slice enwik9_1M        0          1000000
make_slice enwik9_10M       0         10000000
make_slice enwik9_100M      0        100000000   # == enwik8
make_slice enwik9_mid10M    500000000 10000000   # does NOT round-trip through slicetool (scripts/README.md)
# Page-aligned mid slices: they start 29 lines before the first <page> after offset 5e8, so slicetool's
# 29-line "intro" ends exactly at a <page> and pre/post round-trips. Use these instead of enwik9_mid10M.
make_slice enwik9_mid10M_pa 500000168 10000000
make_slice enwik9_mid1M_pa  500000168  1000000
ln -sfn enwik9_100M "$SLICES/enwik8"

# --- 4. verify known md5s ---------------------------------------------------
check_md5() {
  local f="$1" want="$2" got
  got="$(md5sum "$f" | cut -d' ' -f1)"
  if [[ "$got" == "$want" ]]; then
    log "md5 OK   $(basename "$f") $got"
  else
    die "md5 MISMATCH for $f: got $got, expected $want"
  fi
}
check_md5 "$RAW/enwik9"          "$ENWIK9_MD5"
check_md5 "$SLICES/enwik9_100M"  "$ENWIK8_MD5"

# --- 5. sha256 manifest ----------------------------------------------------
cd "$DATA"
if [[ -f "$CHECKSUMS" && $REHASH == 0 ]]; then
  log "verifying against CHECKSUMS.sha256"
  sha256sum -c --quiet CHECKSUMS.sha256 || die "sha256 verification failed"
  log "all sha256 checksums OK"
else
  log "writing CHECKSUMS.sha256"
  sha256sum raw/* slices/* > CHECKSUMS.sha256.tmp
  mv CHECKSUMS.sha256.tmp CHECKSUMS.sha256
  cat CHECKSUMS.sha256
fi

log "done"
