# X3b: the sciences of combining and correcting forecasts

Type: **model addition** (non-transformer; a new *learning rule* for the final combination) + diagnostics.
Date: 2026-11-11. Base: C4 (`patches/exp/C4-combo`). One of three parallel cross-domain darts (X3a, X3b, X3c).

The compressor's back end is a forecast-combination problem: experts emit bit probabilities and a chain of mixers,
SSE, the BITLSTM head and the G1 GLN turns them into one. This round asks what meteorology, econometrics,
psychometrics, sensor fusion and betting would do to that chain, checks each idea against the C3 full-run cost dump
and the G1 final-stage dumps, and builds the one that cleared the bar.

**Result in one paragraph.** At warm full scale the final probability is already calibrated, conditionally too: every
regime, winner, volatility, match-state or byte-class context I tried is worth ≤ 12 KB causally (≤ 31 KB in hindsight)
of 93.3 MB, and no regime exists in which a single expert beats the mix. What the chain does *not* do is use
second-order information: every learner in it is first-order SGD on inputs that are 0.98–0.99 correlated. A
**covariance-aware combiner** (logistic regression by an extended Kalman filter over the 33 final-stage inputs,
after G1) gains **−762 B on enwik9_10M and −384 B on the census 10M slice on top of C4** in an exact causal sim,
where the same inputs under SGD *lose* 498 B. It is built (`patches/exp/X3b-ekf`) and the real build reproduces the sim: **10M 1,260,281 → 1,259,519 (−762 B), 1M −180 B,
census 1M −116 B, all round-trip OK** (§6).
The gain decays over the first tenths of a slice (part of it is faster cold-start learning), so the full-enwik9
ESTIMATE is modest: **about 35 KB (15–55 KB)**. GO by the 400 B bar; not the 100 KB item we were hunting.

## 1. Data and tools

| What | Where |
|---|---|
| C3 full-run cost dump (586,459,020 coded bytes, warm, reordered; per byte: coder's 8 bit probabilities, alone-costs of transformer / fxcm / PPMd, match state) | `build/exp/FULL/cost_full.bin` |
| G1 final-stage dumps of the C3 build (per bit: 26 layer-1 inputs, raw final logit, p after SSE, p after X2, coded p, bit, context bytes) | `build/exp/G1/dump_enwik9_{1M,10M}.bin`, `dump_enwik9_mid{1M,10M}_pa.bin` (`patches/exp/G1-gln/dump.sh`) |
| Record cost logs of the same slices (match state per byte, same coded stream) | `build/exp/A1/runs/cost_{1M,10M}.bin`, `build/exp/E12/runs/cost_mid10M_pa.bin` |
| Full-scale diagnostics + causal recalibration stages on a cost log (streams it, OpenMP over stages) | `scripts/analysis/x3b_full.cpp` |
| Causal bit-level sims **on top of C4**: re-runs `g1_gln.h` exactly with C4's z[3] (reproduces C4's −842.6 / −627.7 / −101.1 B), then each candidate stage reads G1's logit | `scripts/analysis/x3b_sim.cpp` |

All stages learn online from the first bit (no pretraining, no second pass) and use only decoder-side information.
T1 is not in the G1 dumps (it only touches interlanguage titles). Hyperparameters: each stage was given 2–3 rate
settings chosen a priori and all are reported; the chosen EKF setting (P0 1e-3, q 1e-9) was the first one tried
on the 1M dump and is not the best on 10M (q 3e-10 is, by 49 B).

## 2. Survey (22 candidates)

Status: **built** / *simulated* (number = bytes vs C4 on enwik9_10M, exact causal sim, unless marked full) / counted
(= priced from the diagnostics, no sim needed) / skipped.

| # | Home field: mechanism | Analogue in the coder | Covered already? | Result |
|---|---|---|---|---|
| F1 | **Sensor fusion: covariance-aware fusion of correlated sensors (Kalman / information filter); online Newton step** | Final combiner that carries the posterior covariance of its weights (EKF logistic regression) instead of SGD | **No.** Every mixer and G1 neuron is first-order; inputs are 0.98–0.99 correlated (§3) | **built: sim −762 B (10M), −384 B (census 10M); SGD control on the same inputs +498 B** |
| F2 | Meteorology: **EMOS / reliability diagrams by regime** | Recalibrate the final p per regime cell (logit-offset APM) | Mostly: SSE chain, E1 APM, G1 | *sim* +20 to +79 B (worse) on C4 for 11 regime contexts; full C3: −3 to −12 KB causal (§4). **NO-GO** |
| F3 | Econometrics: **regime switching / HMM over the winning expert** | Predict the next winner, gate on it | Mixer selectors | counted: winner persistence is weak (P(fx→fx) 0.605 vs base 0.541); no regime cell where an expert alone beats the mix (26 B total at full scale). *sim* as gate features: below. **NO-GO** |
| F4 | Finance: **volatility clustering (GARCH)** | Recent-cost EMA as calibration context / gate feature | No | counted: cost autocorrelation 0.15–0.19 (lags 1–2), 0.10 at lag 64, so the regime is real, but the mix is calibrated inside each regime: −3.0 KB causal at full scale. **NO-GO** |
| F5 | Tracking the best expert: **fixed-share (Herbster–Warmuth) arithmetic pool** | A linear opinion pool with switching over {G1, tf, fxcm, E2, final mixer, mean mixer, SSE}; all cmix mixing is geometric | No | *sim*: pool alone +462 B; as a 2nd input to a mixer +162 B; its G1-weight as an APM context −159 B. **NO-GO** as a predictor |
| F6 | Forecasting tournaments: **extremizing** | Learn a slope on the final logit, globally and per context | SSE / G1 | *sim* +26 B (global), covered. At full scale on C3 the only global miscalibration is *over*-caution in the tails (§4) |
| F7 | Psychometrics: **item-response model of expert skill by item type** | Skill (EMA of expert loss − mix loss) × content as context | Mixer selectors | *sim* +37 B (APM); full C3 −9.0 KB (skill), −11.8 KB (skill × in-byte cost). **NO-GO** |
| F8 | Sports rating: **Elo/Glicko per context** | Same signal as F7 as GLN gate features | — | *sim* GLN2 regime-only gates −215 B, regime + disagreement −400 B vs −343 B for a second GLN with G1's own kind of gates: the regime features add ≈ 57 B. Small |
| F9 | Wisdom of crowds / signal detection: **vote counts, median** (order statistics a linear pool cannot see) | #mixers above the final logit, median of the 23 mixers, as context / gates / input | G1 gates on the spread and mean only | *sim* APM votes × d(median) −121 B; GLN2 with vote/median/pool gates −459 to −482 B (10M), −244 B (census). Second best; stacks with F1 (−854 B together) |
| F10 | Meteorology: **2-D reliability (joint calibration of forecast pairs), analog ensemble** | APM on the final logit in cells of (tf − p, E2 − p, m1 − p) | G1's half-spaces approximate it | *sim* −228 B (10M), −116 B (census); after F1 still −152 B / −88 B |
| F11 | Control: **innovation-based adaptive gain** | Scale G1's learning rate by recent excess cost | A3 found LR optima flat | *sim* +24 / +65 B (k +0.5 / +1), −5 B (k −0.5). **NO-GO** |
| F12 | Kalman: **random-walk process noise instead of a step-size schedule** | q in F1 | — | part of F1: q 0 −628, 3e-10 −811, 1e-9 −762, 3e-9 −675, 1e-8 −517, 1e-7 +119 B |
| F13 | Hierarchical / mixed-effects models | One EKF weight set per context (bpos, c0, votes, in-byte cost, confidence) | — | *sim* all worse than one set on 10M (−612 by bpos, −788 by 3 confidence levels but −305 vs −384 on census): dilution. Might turn at 586M bytes; untested |
| F14 | Econometrics: **forecast encompassing tests** | Does any input carry information the output lacks? | — | counted: yes — that is what F1's gain is. With SGD the test "fails" (+498 B) only because of conditioning |
| F15 | Signal detection: **confidence saturation** | The coder's probability floor | G1 (logit clip ±12) | counted, full C3: bits forecast at p ≈ 8e-5 come true at 9e-6: 9.2 + 4.1 + 1.6 + 1.1 = **16 KB** of the 18.9 KB global hindsight miscalibration sits in the two extreme bins on each side. On C4 a global recalibration finds nothing (+26 B), so G1 already took it |
| F16 | Within-token surprise (psycholinguistic "garden path") | Cost of this byte's earlier bits as context: after a surprising prefix the model's hypothesis is dead | Partly (c0 contexts) | full C3 −8.2 KB causal (best single small context), on C4 +20 B: G1 covers it. **NO-GO** |
| F17 | Epidemiology: **combining correlated diagnostic tests** (conditional dependence) | Interaction terms between expert logits | G1 gating | *sim* EKF + 3 interaction terms −772 vs −762 B: nothing |
| F18 | Finance: **Kelly / universal portfolio** | Cover's universal portfolio = Bayesian mixture over constant-weight pools | F5 is its switching version | counted with F5 |
| F19 | Meteorology: **BMA with per-member spread** | Mixture with member-specific variance | F5/F1 | skipped (subsumed) |
| F20 | Stacking with more members | G1's 8 neuron logits as extra EKF inputs (41) | — | *sim* −782 vs −762 B: nothing |
| F21 | Survival analysis: **hazard since the last surprise** | Bytes since a ≥ 4-bit byte as context | — | full C3 −3.3 KB. **NO-GO** |
| F22 | Second-order learning **inside** the record's own final (layer-1) mixer and the 23 layer-0 mixers (replace SGD) | — | No | **untested lead** (§7): cannot be simulated from a final-stage dump |

Not repeated from earlier rounds: X1 P4b (Kalman-gain weight updates) was dismissed there in one line as "can't be
re-simulated from the final pbits; ≈ 50 B recalibration left". That pricing was wrong for the *final stage*: the G1
dump (which did not exist then) has the stage's inputs, and F1 is that idea, measured. X2 A16 (PID learning rates) = F11, re-checked, still dead.

## 3. Diagnostics at warm full scale (C3 dump, measured)

`build/exp/X3b/x3b_full build/exp/FULL/cost_full.bin 0 8` (about 20 min, 8 threads, 300 MB RSS).

- Totals: final 93,313,033 B; transformer alone 96,392,241; fxcm alone 94,633,345; PPMd alone 157,962,059.
  At full scale fxcm alone beats the transformer alone (on the 10M slice it is the reverse: 1,277,500 vs 1,275,720).
- **Experts' errors are almost perfectly correlated.** Correlation of per-byte costs: final–tf 0.983, final–fxcm
  0.993, tf–fxcm 0.981, PPMd–others 0.77. (On the 10M slice: 0.994 / 0.995 / 0.988.)
- **No winning regime.** Per-byte winner shares tf 35.2%, fxcm 54.1%, PPMd 10.7%. Transition matrix rows
  (tf, fx, ppmd): 0.422/0.467/0.110, 0.302/0.605/0.093, 0.375/0.456/0.169 — persistence is +6 to +7 points over
  the base rate. In 675 regime cells (winner t−1 × winner t−2 × three skill EMAs) the cells where an expert *alone*
  beats the mix total 2 B (tf), 26 B (fxcm), 0 B (PPMd). The per-byte oracle (min of experts and mix) is 11.5 MB
  below the mix, but nothing causal predicts which byte.
- **Burstiness is real and already priced.** Cost autocorrelation 0.149 / 0.192 / 0.138 / 0.126 / 0.115 / 0.106 /
  0.100 / 0.084 at lags 1 / 2 / 4 / 8 / 16 / 32 / 64 / 256. Mean next-byte cost runs from 0.17 bits (fast EMA < 0.1,
  19.3% of bytes) to 2.1 bits (EMA ≥ 4.5); from 2.04 bits right after a ≥ 4-bit byte to 0.11 bits 2,048+ bytes later.
  39.7% of bytes cost < 0.01 bit; bytes ≥ 4 bits are 12.1% of bytes and 58.7% of bits.
- **Where the bits are (coded-byte class):** WRT/UTF-8 bytes 0xC0–0xFF 42.4% of bits (2.48 bits/byte), 0x80–0xBF
  23.3% (1.85), lowercase 11.6%, punctuation 8.0%, digits 6.4%.

## 4. Conditional calibration (measured)

Per context set: hindsight = Σ n·KL(empirical rate ‖ mean forecast) over (cell × 24 logit bins), minus 0.72 bit per
occupied bin; causal = online logit-offset APM per cell (best of three rate settings).

| context (full C3 dump) | cells | hindsight (B) | causal online (B) |
|---|---:|---:|---:|
| none (global reliability diagram) | 1 | 18,922 | −7,098 |
| partial byte c0 | 256 | 21,872 | −4,110 |
| bit position × cost so far in this byte | 48 | 22,300 | −8,214 |
| bit position × EMA cost fast × slow (volatility regime) | 384 | 24,378 | −3,021 |
| bit position × winner(t−1) × winner(t−2) × mix-beat-all | 144 | 22,662 | −3,868 |
| bit position × skill(tf) × skill(fxcm) × skill(PPMd) | 600 | 28,325 | −8,984 |
| bit position × bytes since a surprise | 96 | 22,706 | −3,257 |
| bit position × match state (cm, fxcm: length × expected bit) | 1,152 | 26,783 | −5,994 |
| bit position × class(b−1) × class(b−2) | 512 | 23,174 | −673 |
| bit position × cost(b−1) × cost(b−2) | 288 | 23,408 | −3,584 |
| bit position × file position (20ths) | 160 | 20,731 | +669 |
| c0 × cost so far | 1,536 | 27,468 | −6,639 |
| bit position × match-expect(cm, fx, sm) × winner × cost so far | 9,216 | 30,541 | −9,236 |
| c0 × match-expect × cost so far | 12,288 | 29,216 | −7,172 |
| **bit position × skill(tf) × skill(fxcm) × cost so far** | 1,200 | **30,583** | **−11,773 (−0.013%)** |

- The largest reliable miscalibration is unconditional and sits in the tails: 981M bits forecast at p = 8.1e-5 come
  true at 9e-6 (9.2 KB), 485M at 1.9e-4 vs 7.6e-5 (4.1 KB), 447M at 1 − 6.7e-5 vs 1 − 2.3e-5 (1.6 KB), 316M at
  1 − 3.1e-5 vs 1 − 3e-6 (1.1 KB). Everything between logit −3 and +3 is calibrated to < 0.1 KB per bin.
- Conditioning adds at most 11.7 KB of hindsight value over the global diagram (30.6 vs 18.9 KB).
- These are C3 numbers. On top of C4 (G1) the same stages on the 10M dump all *lose* 20–79 B; G1 already
  appears to remove the tail error (a global recalibration gains 82 B on the record's 10M log and loses 26 B after G1;
  not checked bin by bin).

**Verdict on the seed questions:** conditionally calibrated — yes, to within 0.03% in hindsight; latent winner
regime — no; innovation-driven gains — no; correlated errors a covariance-aware combiner can exploit — **yes**.

## 5. Combiner sims on top of C4 (measured, exact causal sims)

`build/exp/X3b/x3b_sim <G1 dump> <record cost log>`; `X3B_SET=2` for the second table. Bytes vs C4's coded cost
(10M: 1,260,814.6 B; census 10M: 322,853.3 B in the sim).

| stage after G1 | enwik9_10M | enwik9_mid10M_pa | 10M by tenth of the dump |
|---|---:|---:|---|
| SGD mixer control, 10 inputs, lr 0.0003 | +498 | +19 | +27 +49 +54 +47 +50 +50 +63 +58 +62 +38 |
| EKF, 10 inputs (G1, tf, fxcm, E2, m1, mean mixer, SSE, pfin, coded p, bias) | −355 | −284 | −100 −48 −34 −33 −29 −31 −17 −21 −15 −27 |
| **EKF, 33 inputs (+ the 23 layer-0 mixers, their median), P0 1e-3, q 1e-9 — as built** | **−762** | **−384** | −197 −106 −73 −69 −63 −64 −45 −45 −43 −57 |
| … q 3e-10 / 3e-9 / 0 / 1e-8 | −811 / −675 / −628 / −517 | −381 / −369 / −285 / −320 | |
| … P0 1e-2 | −800 | −447 | (all of the difference is in the first tenth) |
| … one weight set per bit position | −612 | −207 | |
| … 3 sets by confidence | −788 | −305 | |
| … + G1 neuron logits (41) / + 3 interaction terms (36) | −782 / −772 | −393 / −411 | |
| second GLN, G1's kind of gates (control) | −343 | −260 | −83 −48 −36 −39 −33 −25 −25 −19 −15 −22 |
| second GLN, vote / median / pool gates (F9) | −459 | −244 | |
| second GLN, regime + disagreement gates (F8) | −400 | −230 | |
| 2-D reliability APM on (tf, E2, m1) differences (F10) | −228 | −116 | |
| EKF → F10 APM | −914 | −472 | −215 −127 −92 −86 −79 −79 −59 −59 −58 −60 |
| EKF → second GLN (regime gates) | −890 | −477 | |
| EKF → F10 APM → second GLN (three stages) | −1,008 | −549 | |

Reading:
- The combiner matters, not the inputs: SGD on the same 10 inputs is worse than not having the stage at all.
- One global weight set is best at this length; per-context sets dilute.
- The gain is front-loaded: the first tenth carries 26% of it (cold start: the EKF converges in thousands of bits
  where the SGD mixers upstream need millions). Tenths 4–10 run at −43 to −73 B per 576K coded bytes on 10M
  (≈ 99 B per million coded bytes; the last four ≈ 82) and −15 to −36 B per 600K on the census slice (≈ 45 per million).
- q = 0 (a pure Bayesian posterior, gain → 0) decays fastest (−16 B in the last tenth), so tracking is worth
  having; q ≥ 1e-8 is too noisy.

## 6. Build and benches

`patches/exp/X3b-ekf/`: `x3b_ekf.h` (the filter; doubles, 9.3 KB of state), `0001-X3b-ekf-after-G1.patch`
(inside `Predictor::G1Adjust`, update in `Perceive` next to G1's; on exactly when G1 is; no env),
`build.sh` (C4's tree + this, same PGO pipeline and guards), `run.sh`, `ubench.cpp`.

Real build, packed exe, clean environment, through `scripts/bench.sh` (ledger rows `fx2T+C4+X3b-ekf`, all
round-trip byte-exact with `cmp`; code commit f81ec36):

| slice | C4 (ledger) | C4 + X3b | delta | exact sim |
|---|---:|---:|---:|---:|
| enwik9_1M | 126,132 | **125,952** | **−180 (−0.143%)** | −182.2 |
| enwik9_10M | 1,260,281 | **1,259,519** | **−762 (−0.060%)** | −762.0 |
| enwik9_mid1M_pa (census) | 29,471 | **29,355** | **−116 (−0.39%)** | (not run on this dump) |
| enwik9_mid10M_pa (census) | — | — (sim only) | | −383.6 |

Peak RSS 5,792 MiB on 10M (C4: 5,755; within run-to-run noise of the slice mode). The 1M row's note says
"sim: n/a"; the sim value for that configuration is −182.2 B.

**Cost.**
- Size: packed exe 202,272 B vs C4's 200,960 = **+1,312 B, counted twice = +2.6 KB of S**.
- RAM: 9.3 KB.
- Time: `ubench` 436 ns per coded bit = **3.5 µs per coded byte** on the (loaded) dev box, i.e. 586M × 3.5 µs ≈
  0.57 h per direction here, **≈ 0.85 h on the reference machine (×1.5; ESTIMATE)**. That is the plain triple loop
  (2 × 33² double multiply-adds per bit); using the symmetry of P, or the 10-input version (about 10× cheaper,
  −355 / −284 B), would cut it. Slice wall times in the ledger are contended and say nothing.

## 7. Estimate, verdict, next

**Full enwik9 ESTIMATE: about −35 KB (range −15 to −55 KB), net of the +2.6 KB exe.** Basis: the sim's steady-state
rate after the cold start, 40–99 B per million coded bytes (census low, prose high) × 586M coded bytes = 23–58 KB,
shaded down because the per-tenth gain was still drifting lower at the end of both 10 MB slices and a warm
full-run mixer chain leaves less to fix; the cold-start bonus (≈ 0.2 KB) does not scale. Not verified at scale:
there is no full-run dump of the final stage's inputs (the 23 GB cost dump has the output only).

**Verdict: GO by the bar (≥ 400 B causal on 10M on top of C4), as a small item — fold into the next combo.** It does
not close the 330 KB gap; with the three-stage stack (EKF → 2-D APM → vote/regime GLN, sim −1,008 / −549 B) the
family is worth perhaps 45–70 KB (ESTIMATE, same method).

Everything else in the survey is NO-GO (attic/2026-11-11-X3b-forecast-calibration/NOTES.md).

**Best untested lead (F22):** the same disease — first-order learning on near-collinear inputs — sits *inside* the
record: the layer-1 final mixer (26 inputs, one global weight set, lr 0.0003) and the 23 layer-0 mixers, each over
hundreds of correlated model outputs with thousands of weight sets. X3b only repairs the last node, after the
fact, and still finds 0.06%. A second-order or whitened update where the inputs are mixed (full EKF for the
26-input final mixer: same cost as X3b; diagonal/Adam-style or low-rank preconditioning for the layer-0 mixers,
whose 15.6% time share makes full covariance impossible) is the direct way to ask how much the conditioning costs
upstream. It needs the real build, not a dump: change `layers_[1]`'s update first (one afternoon, exact A/B on 10M).
Second lead: a full-run-scale test of per-context EKF sets (F13), which lose on 10 MB only through dilution.

## 8. Reproduce

```sh
g++ -O2 -march=x86-64-v3 -fopenmp -std=c++17 -include algorithm -o build/exp/X3b/x3b_full scripts/analysis/x3b_full.cpp
build/exp/X3b/x3b_full build/exp/FULL/cost_full.bin 0 8 > build/exp/X3b/full_c3.txt           # §3, §4
g++ -O2 -march=x86-64-v3 -std=c++17 -o build/exp/X3b/x3b_sim scripts/analysis/x3b_sim.cpp
build/exp/X3b/x3b_sim build/exp/G1/dump_enwik9_10M.bin build/exp/A1/runs/cost_10M.bin         # §5 (about 15 min)
X3B_SET=2 build/exp/X3b/x3b_sim build/exp/G1/dump_enwik9_mid10M_pa.bin build/exp/E12/runs/cost_mid10M_pa.bin
patches/exp/X3b-ekf/build.sh                                                                  # needs build/exp/C4's inputs
patches/exp/X3b-ekf/run.sh enwik9_1M "..." --fast; patches/exp/X3b-ekf/run.sh enwik9_10M "..."
```
