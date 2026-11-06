# A3: encoder-side selection with decoder replay (E2 logit head LR, per block)

Type: **architectural** (a new way of spending the compute budget). Base: fx2-cmix-transformer-v1 + E2 logit head
(`patches/exp/E2-correction-head`), frozen weights. Date: 2026-11-06. Status: **NO-GO** (see
`docs/design/A3-encoder-select.md` and `attic/2026-11-06-A3-encoder-select/NOTES.md`).

## Mechanism tested

The record is symmetric: the decoder repeats every computation of the encoder. Here the encoder would instead fork
the state of a cheap component at each block of B tokens, run the block K times with K variants (here: K learning-rate
multipliers of the E2 logit head), keep the best, commit its state and send the choice (≤ log2 K bits per block). The
decoder runs only the chosen variant, so it pays 1×, the encoder K×.

This directory holds only the **measurement** step (no coder change was built, because the bound failed the
decision rule):

- `0001-a3-dump.patch`: applies on top of `patches/fx2-cmix-transformer/*.patch` +
  `patches/exp/E2-correction-head/*.patch`. With `A3_DUMP=path` and `E2_LOGIT=1`, the transformer writes, for every
  token the E2 head predicts, the record `int32 y, float xn[192], float raw_logits[205]` (1,592 B/token). That is
  everything the logit head reads, i.e. what an encoder would buffer per block.
- `scripts/analysis/a3_sim.cpp`: replays the E2 logit head on a dump (same op order, glue.cpp's softcap/softmax).
  Modes: `indep` (K independent heads), `fork` (greedy fork-and-commit), `forkla` (fork-and-commit scored with an
  H-block lookahead under a default multiplier), `fixed` (replay a choice file).
- `scripts/analysis/a3_oracle.py`: per-block oracle over the `indep` losses, side-info cost (naive log2 K and
  order-1 adaptive), and a fixed-share mixture of the K heads (the symmetric, K×-decoder alternative).

## Reproduce

```sh
T=build/exp/A3/tree; rsync -a --exclude .git --exclude 'models/*.tch' third_party/fx2-cmix-transformer-v1/ $T/
(cd $T && git init -q && for p in ../../../../patches/fx2-cmix-transformer/*.patch \
   ../../../../patches/exp/E2-correction-head/*.patch ../../../../patches/exp/A3-encoder-select/*.patch; do git apply $p; done)
patches/exp/E2-correction-head/build.sh $T build/exp/A3/cmix_dump
patches/exp/A3-encoder-select/dump.sh 10M $PWD/build/exp/E2/proxy/10M.pre     # transformer-only, ~17 min, 1.2 GB RSS, 8.8 GB dump
O=$T/cpp_infer/src/opt; g++ -O3 -march=x86-64-v3 -std=c++17 -fno-math-errno -I$O scripts/analysis/a3_sim.cpp $O/glue.cpp -o build/exp/A3/a3_sim
M=0,0.25,0.354,0.5,0.707,1,1.414,2,2.828,4
build/exp/A3/a3_sim indep build/exp/A3/dump/10M.bin build/exp/A3/sim/10M_indep.f32 $M
python3 scripts/analysis/a3_oracle.py build/exp/A3/sim/10M_indep.f32 $M --mix
build/exp/A3/a3_sim fork   build/exp/A3/dump/10M.bin out.u8 16384 $M
build/exp/A3/a3_sim forkla build/exp/A3/dump/10M.bin out.u8 16384 $M 4 0.5
```

`dump.sh` is a 10-line wrapper (`E2_LOGIT=1 A3_DUMP=... cmix -c ... --transformer W --transformer-only`).
The `.pre` inputs are the record's slice-mode preprocessed slices (`slicetool pre`).

## Results

See `docs/design/A3-encoder-select.md`.
