#!/usr/bin/env bash
# X3c: PPMD-only (the real component, cmix --ppmd-only) over 100 MB windows of the record's article order in several
# orders, two at a time (model files on /dev/shm, see x3c_ppmdonly.sh). Samples: x3c_mksample.py; names:
#   A100 = the first 100 MB of pages (id order vs record rank), B/C/D/E100 = 100 MB from record rank 60000 / 110000 /
#   5000 / 140000 in record order (rec), sorted by page id inside windows of N (wN), fully sorted (id), or seriated
#   by text overlap (ser, x3c_seriate.py). Results: build/exp/X3c/ppmd/<name>.{size,progress.log}; region split with
#   x3c_ppmdcmp.py and the coded streams in build/exp/X3c/streams/ (cmix -s).
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
S=build/exp/X3c/samples; M=scripts/analysis/x3c_mksample.py; R=$PWD/build/refs/fx2-cmix-transformer-v1; X=$PWD/build/exp/X3c
run2() {  # name1 args1... ; runs two jobs in parallel
  local pids=()
  for spec in "$@"; do
    n=${spec%%|*}; a=${spec#*|}
    ( python3 -P -E $M $a --out $S/$n.xml > /dev/null && mkdir -p $X/pre && $R/slicetool pre $S/$n.xml $X/pre/$n.pre > /dev/null 2>&1 && rm $S/$n.xml \
      && scripts/analysis/x3c_ppmdonly.sh $n $X/pre/$n.pre && mkdir -p $X/streams/w_$n && (cd $X/streams/w_$n && $X/../C4/bin/cmix -s $R/tree/dictionary/english.dic $X/pre/$n.pre ../$n.stream > /dev/null 2>&1); rm -rf $X/streams/w_$n $X/pre/$n.pre ) &
    pids+=($!)
  done
  wait "${pids[@]}"
}
run2 "A100_id|--set bytes:page:0:100000000 --order id" "A100_rec|--set bytes:page:0:100000000 --order rec"
run2 "B100_rec|--set bytes:rank:60000:100000000 --order rec" "B100_id|--set bytes:rank:60000:100000000 --order id"
run2 "B100_w1024|--set bytes:rank:60000:100000000 --order rec --window 1024" "B100_w256|--set bytes:rank:60000:100000000 --order rec --window 256"
python3 -P -E scripts/analysis/x3c_mksample.py --set bytes:rank:60000:100000000 --order rec --out $S/B100_rec.xml > /dev/null
python3 -P -E scripts/analysis/x3c_seriate.py $S/B100_rec.xml.pages $X/orders/B100_ser.txt
run2 "B100_ser|--set bytes:rank:60000:100000000 --order file:$X/orders/B100_ser.txt" "C100_rec|--set bytes:rank:110000:100000000 --order rec"
run2 "C100_w1024|--set bytes:rank:110000:100000000 --order rec --window 1024"
run2 "B100_w4096|--set bytes:rank:60000:100000000 --order rec --window 4096" "D100_rec|--set bytes:rank:5000:100000000 --order rec"
run2 "D100_w1024|--set bytes:rank:5000:100000000 --order rec --window 1024" "E100_rec|--set bytes:rank:140000:100000000 --order rec"
run2 "E100_w1024|--set bytes:rank:140000:100000000 --order rec --window 1024" "C100_w256|--set bytes:rank:110000:100000000 --order rec --window 256"
run2 "D100_w256|--set bytes:rank:5000:100000000 --order rec --window 256" "B100_w512|--set bytes:rank:60000:100000000 --order rec --window 512"
echo chain-done
