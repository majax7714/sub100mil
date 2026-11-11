# Attic

Strategies that failed or were superseded. **We keep them, we don't delete them.** Each one gets a directory
`attic/<YYYY-MM-DD>-<slug>/` containing:

- `NOTES.md`: the hypothesis, what was built, results (slice, bytes, bpb, time, RSS, commit),
  why it failed or was dropped, and what we'd try differently
- the code, or a pointer to the commit(s) where it lived if it's too large or tangled to copy

## Index

| Date | Slug | One-line verdict |
|------|------|------------------|
| 2026-11-05 | [A1-retrieval](2026-11-05-A1-retrieval/NOTES.md) | No-go: the record + memorized transformer already capture repetition; realistic gain is about 10–20 KB |
| 2026-11-05 | [A2-link-targets](2026-11-05-A2-link-targets/NOTES.md) | No-go: the record already ranks link targets far better than a prefix candidate set (7.6 vs 13.1 bits per seen link); sim +208 B on 10M, about +17 KB (0–40 KB) projected on enwik9 |
| 2026-11-05 | [E11-in-article-cache](2026-11-05-E11-in-article-cache/NOTES.md) | No-go: an in-article token cache gains +702 B on 10M (0.056%); repeats of rare words gain but first uses lose; about 60 KB (45–90 KB) projected on enwik9, and E10 dyneval targets the same signal |
| 2026-11-06 | [A3-encoder-select](2026-11-06-A3-encoder-select/NOTES.md) | No-go: encoder-side per-block LR selection for the E2 head (fork, try K, commit, send choice); oracle bound −149 B head loss (≈ −83 B coded) on 10M vs the best fixed LR, greedy fork-and-commit loses; LR optima are flat, and encoder slack is only 7.8 h |
| 2026-11-06 | [X1-cross-domain](2026-11-06-X1-cross-domain/NOTES.md) | No-go: 25 physics/biology/chemistry mechanisms surveyed; best (Potts couplings between an article's tokens) +0.03% on 10M over a warm control, about 35 KB (10–60 KB) projected; coarse-state contexts ≤ 28 B. Side finding: the slice cold-start penalty is about 3% for CM-side vocabulary |
| 2026-11-06 | [X2-text-laws](2026-11-06-X2-text-laws/NOTES.md) | Sub-results of X2 (which is a GO for the metadata time law): domain laws in article text hold about 150 KB at stake, sims +327 B on 10M, about 14 KB projected; the page-id-from-article-order law (≈ 165 KB simulated) isn't decoder-valid because archive9 has no order file |
| 2026-11-07 | [P1-e12-x2-Os](2026-11-07-P1-e12-x2-Os/NOTES.md) | No-go: `#pragma GCC optimize("Os")` on the E12/X2 namespaces saves 1,472 B of .text but the packed exe grows 40 B (LTO/PGO and std template instances keep most of the code at -O3) |
| 2026-11-07 | [N1-hcache](2026-11-07-N1-hcache/NOTES.md) | No-go: continuous cache / kNN-LM keyed on the transformer's hidden state, offline on top of the C3 E2 head; 10M head-level −1,504 B (LOG, W 8192) but only −138 B against the record's final model (−83 B at an affordable 32-d key, ≈ 3.3 µs/byte); the transformer covers in-article copies and the CM covers cross-article ones |
| 2026-11-11 | [X3a-provenance-laws](2026-11-11-X3a-provenance-laws/NOTES.md) | Sub-results of X3a (which is a GO for the order rule): 14 stream-side provenance laws (rev ids, page-id ranks, user names, redirect titles, section comments, …) hold about 60 KB of oracle-gated gain in total; the metadata block is now mostly information |
| 2026-11-11 | [X3b-forecast-calibration](2026-11-11-X3b-forecast-calibration/NOTES.md) | Sub-results of X3b (EKF combiner is a small GO): regime recalibration, HMM winner regime, fixed-share pool, extremizing, innovation gain all lose on top of C4; C3's total hindsight miscalibration is 18.9 KB |
| 2026-11-11 | [X3c-arrangement](2026-11-11-X3c-arrangement/NOTES.md) | No-go: the record's article order is right for this stack; identity order est. 540–930 KB of loss against 397 KB saved, text-overlap seriation +0.03–0.48% on PPMD; windowed id-sort about 45 KB (−25 to +85), about zero after X3a's order rule |
