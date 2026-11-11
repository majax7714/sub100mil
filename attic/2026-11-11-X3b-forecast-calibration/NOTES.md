# X3b sub-results: regime-wise recalibration, winner regimes, pools and adaptive gains (NO-GO)

Date: 2026-11-11. Part of X3b (docs/design/X3b-forecast-combination.md), whose EKF combiner is a GO
(patches/exp/X3b-ekf). These are the forecast-combination ideas that failed. Code: `scripts/analysis/x3b_full.cpp`
(full cost dump) and `scripts/analysis/x3b_sim.cpp` (on top of C4, G1 dumps); commit hashes in the design doc's log.

## Ideas
1. **Conditional recalibration (EMOS, reliability by regime).** Recalibrate the final probability per cell of a
   regime context: volatility (EMA of recent cost), winner history, expert skill (EMA of expert loss − mix loss),
   time since a surprise, match state, byte classes, cost of the current byte's earlier bits, file position.
2. **Latent winner regime (HMM / tracking the best expert).** Predict which expert wins next and gate on it.
3. **Fixed-share arithmetic pool** (linear opinion pool with switching) as an extra predictor.
4. **Innovation-driven gain** for G1's learning rate.
5. **Extremizing** (global or per-context slope on the final logit).

## Numbers (all measured)
- Full C3 dump, 586M coded bytes, 93,313,033 B. Global hindsight miscalibration 18.9 KB (0.020%), 16 KB of it in the
  extreme logit bins (forecast 8.1e-5, observed 9e-6). Best conditional context (bit position × skill(tf) ×
  skill(fxcm) × cost so far, 1,200 cells): 30.6 KB hindsight, **−11.8 KB causal online**. Volatility regime −3.0 KB,
  winner history −3.9 KB, time since surprise −3.3 KB, match state −6.0 KB, in-byte cost −8.2 KB, byte classes −0.7 KB.
- On top of C4 (10M G1 dump): the same 11 regime contexts **lose 20–79 B**; global slope +26 B.
- Winner regime: transition matrix persistence is 6–7 points over base rate; over 675 regime cells an expert alone
  beats the mix by 28 B in total. Expert byte costs are 0.98–0.99 correlated.
- Fixed-share pool over 7 experts: alone +462 B vs C4 on 10M (+4,732 B on census 10M); as a second mixer input
  +162 B; its posterior weight as an APM context −159 B.
- Innovation gain on G1: +24 B (k 0.5), +65 B (k 1), −5 B (k −0.5) on 10M; +69 / +227 / −39 B on census 10M.
- A second GLN gated only on regime features: −215 B (10M); a second GLN with G1's own kind of gates: −343 B.

## Why they failed
The chain (context-selected mixers → SSE → E1 APM → BITLSTM → G1) is a very good calibrator already. Regimes exist
(cost autocorrelation 0.15–0.19 at lags 1–2, still 0.08 at lag 256) but the mix is calibrated inside each of them,
because its inputs (the experts' own confidences) already carry the regime. Experts never dominate the mix in any
causally identifiable regime, so switching has nothing to switch to; an arithmetic pool is strictly worse than the
geometric one here. G1 absorbed the one sizeable calibration error (the probability floor in the tails).

## What we'd try differently
- Stop looking for *contexts* at the final stage: 15 context sets × 3 rates say there is ≤ 0.013% there.
- The winning direction was the learning rule (second-order), not the features. Apply it upstream (design doc §7, F22).
- Per-context EKF weight sets lost on 10 MB through dilution only; they need a ≥ 100 MB final-stage dump to judge.
