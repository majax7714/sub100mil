# G1: gated linear network (GLN) as a final mixing stage on C3

Type: **model addition** (new predictor class for mixing; non-transformer). Base: C3 (`patches/exp/C3-combo`). Date: 2026-11-07.

## Idea

Every mixer in the record is linear in the stretch domain with context-selected weight sets, and the final
(layer-1) mixer has a single global weight set (zero context, lr 0.0003) over its 26 inputs. A GLN neuron
(Veness et al. 2017; AAAI 2021) is also a geometric mixer, but its weight set is selected by **half-space
gates**: K random fixed hyperplanes over side information z, so 2^K regions of the side-info space, each with
its own weights and trained on the neuron's own log loss (local convex problem, fast online convergence).

## Design (as built)

- Placement: **after the BITLSTM head** (E3's `bit_head::Adjust` in the coder), on the coder's 16-bit
  probability: `p = p_->G1Adjust(p)` in `encoder.cpp`/`decoder.cpp`, update at the top of `Predictor::Perceive`.
  It's the last stage, so an offline sim over a dump is exact. Override bits (transformer certain) are skipped.
- Base inputs x (8, one AVX register): fxcm final, transformer byte mixer, E2, final-mixer raw logit,
  mean of the 23 layer-0 mixer outputs, stretch(p after SSE), stretch(coded p), bias 0.5.
- Side info z (8): stretch(coded p) and disagreement features (transformer − p, fxcm − p, E2 − p,
  final-mixer logit − p, mean mixer − p, spread max−min of the 23 mixers), E1's per-bit confidence bucket.
- 8 neurons × 8 gates (256 regions each, 2,048 weight rows, 64 KB). Weights start as identity on stretch(coded p),
  so unvisited regions change nothing. Output = mean of the 8 neuron logits (a fixed, non-learned layer 2).
- Update: `w -= lr_n * (p_j - y) * x`, clipped to ±8; `lr_n = lrmin + (lr - lrmin) * tau / (tau + n_region)`,
  lr 0.001, lrmin 0.0003, tau 64. Hyperplanes N(0,1) normalized, offsets N(0, 0.15²), mt19937 seed 923.
- AVX2 (`-march=x86-64-v3`): gate projections 8-wide per neuron, `movemask` gives the region index, table
  logistic (4,097 knots, linear interpolation; no `exp` per call). Deterministic: same binary, fixed op order.
- Runtime switch `G1_GLN=1` (both directions); `G1_N/K/LR/LRMIN/BSCALE/SEED/TAU` override. Like E12/X2 these
  must become compile-time defaults for a submission.

## Files

- `g1_gln.h` (copied to `src/models/g1_gln.h`) and `0001-G1-gln-final-stage.patch` (predictor + coder hooks).
- `build.sh`: C3's build + step 11; PGO training with `G1_GLN=1`; output `build/exp/G1/bin/cmix`.
- `run.sh <name> <slice> <notes> [--fast]`: C3's env + `G1_GLN=1` (method `fx2T+G1-<name>`).
- `tool-g1-dump.patch` + `dump.sh <slice> [--fast]`: observation-only dump of the final stage (per bit: the 26
  layer-1 inputs, raw final logit, p after SSE, p after X2, coded p, bit, override, partial byte, last 2 bytes,
  E1 buckets; 132 B/bit, 10M = 5.9 GB). Apply to the C3 tree. Archive size is unchanged (1M 126,244, 10M 1,261,700).
- `sim/glnsim.cpp` (research sim: layers, gating options, feature sets), `sim/g1sim.cpp` (exact sim of `g1_gln.h`),
  `sim/grid.sh`, `sim/ubench.cpp` (isolated cost).

## Results

See the ledger rows `fx2T+G1-v2` and the summary in this directory's history / BUILDLOG.
