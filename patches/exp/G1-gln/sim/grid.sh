#!/usr/bin/env bash
# grid.sh DUMP "cfg1" "cfg2" ... : runs glnsim configs 8 at a time, prints cfg: result
D=$1; shift; i=0; T=$(mktemp -d)
for c in "$@"; do ( echo "$c: $(./glnsim $D $c | sed 's/.*base/base/')" > $T/$i ) & i=$((i+1)); (( i % 8 == 0 )) && wait; done; wait
for f in $(ls $T | sort -n); do cat $T/$f; done; rm -rf $T
