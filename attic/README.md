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
