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

## Results (ledger rows `fx2T+G1-v2`, all round-trip byte-exact)

| slice | C3 | G1 (real build) | delta | exact sim |
|---|---:|---:|---:|---:|
| enwik9_1M | 126,244 | **126,151** | −93 (−0.074%) | −92.6 |
| enwik9_10M | 1,261,700 | **1,260,910** | −790 (−0.063%) | −801 |
| enwik9_mid1M_pa (census) | 29,760 | **29,685** | −75 (−0.25%) | −74.7 |
| enwik9_mid10M_pa (census) | 323,524 | — (sim only) | ≈ −583 (−0.18%) | −582.6 |

C3's census numbers are from the dump runs (`cmix_dump`, compress only, output identical to C3's binary; not
in the ledger). The real build reproduces the sim to ≤ 11 B (the predictor is compiled with `-ffast-math`,
the sim isn't).

**Not a cold-slice artifact:** the 10M gain is flat over the file, about −70 to −95 B per 4.6M bits
(~1 MB of input) in every tenth; census 10M is flat too (−74 → −42 B per 5M bits, slight decay in the last third).

**Tuning (exact sim on the 10M dump, all within ±60 B):** lr schedule barely matters once lrmin ≈ 0.0002–0.0005;
seeds ±20 B; n 16 −861 B (2× cost); n 4 −768 B; k 7 −778 B; gating on the BITLSTM's own delta
(z[3] = stretch(p_fin) − stretch(coded p)) −840 B at n 8, a free +40 B not yet in the build.
**Didn't help:** all 26 layer-1 inputs as base inputs or as side info (+29 B on 1M, noisy), a learned
output layer (−768/−789 vs −801 for the plain mean), bit-position gating (−597), adding bpos/E1-byte-bucket/
pre-head p to z (−499), a 2-input blend of the GLN with the coded p (worse than using the GLN output directly).

**Cost (ESTIMATE, isolated microbenchmark `sim/ubench.cpp`, dev box):** 131 ns per coded bit (1.05 µs per coded
byte, ~1,000 instructions per bit) for n 8 k 8; the first scalar version with `exp` was 520 ns. Full enwik9:
586M coded bytes × 1.05 µs ≈ 615 s ≈ 0.17 h per direction on the dev box, ≈ 0.26 h on the reference machine
(×1.5); allow up to 2× for cache pressure in situ (64 KB of weights) ≈ 0.5 h. That's 0.6–0.9 µs per enwik9 byte,
under the 3 µs budget. RAM ≈ 80 KB. Slice timings in the ledger are contended and say nothing.

**Full enwik9 gain (ESTIMATE):** −790 B over 5.76M coded bytes is −137 B per million coded bytes; × 586M ≈ 80 KB,
census rate similar (~97 B per million). Range 50–90 KB, assuming the rate holds on the warm, reordered full run.
