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
