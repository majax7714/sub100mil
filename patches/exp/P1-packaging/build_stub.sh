#!/usr/bin/env bash
# build_stub.sh <in-exe> <out-exe>: pack <in-exe> with pack.c and link the self-unpacking stub around it.
# Deterministic: fixed flags, no build-id, no timestamps.
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IN=$(realpath "$1"); OUT=$(realpath -m "$2")
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
gcc -O2 -o "$W/pack" "$D/pack.c"
"$W/pack" "$IN" "$W/payload.bin"
cd "$W"
gcc -Os -march=x86-64 -mtune=generic -static -nostdlib -nostartfiles -ffreestanding -fno-pie -no-pie \
  -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fcf-protection=none -fno-ident \
  -I"$D" -Wl,--build-id=none -Wl,-z,noseparate-code -Wl,-s -Wl,--gc-sections -Wl,-z,norelro -Wl,-z,noexecstack \
  -o "$OUT" "$D/stub.c"
chmod +x "$OUT"
echo "$(stat -c %s "$IN") -> $(stat -c %s "$OUT") ($OUT; payload $(stat -c %s payload.bin))"
