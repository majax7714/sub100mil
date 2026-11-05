# Build log

Newest entries first. One entry per session or experiment. Format:

```
## YYYY-MM-DD: short title
- **Did:** ...
- **Numbers:** slice → bytes (bpb), time, RSS. Ledger rows added: N
- **Learned:** ...
- **Next:** ...
```

---

## 2026-11-04: project bootstrap
- **Did:** Created repo scaffolding: AGENTS.md/CLAUDE.md rules, build log, attic, results ledger,
  .gitignore for data/. Fetched enwik9, derived enwik8 and working slices (`scripts/fetch_data.sh`).
  Researched the official prize rules and compute limits into `docs/RULES.md`.
- **Numbers:** pending (baseline general-purpose compressors on slices)
- **Learned:** Dev box has 12 cores / 30 GiB. No C++ toolchain installed yet.
- **Next:** Install g++/cmake. Set up a benchmark harness (compress → decompress → cmp → ledger row).
  Get a reference CM baseline on slices.
