# sub100mil

An attempt to compress **enwik9** (the Hutter Prize corpus) to a total size of
**under 100,000,000 bytes** (compressed archive + decompressor), within the prize's compute limits.

- Rules and constraints: [docs/RULES.md](docs/RULES.md)
- Progress log: [BUILDLOG.md](BUILDLOG.md)
- Measurements: [results/ledger.tsv](results/ledger.tsv)
- Failed or superseded attempts: [attic/](attic/README.md)
- Working rules for agents: [AGENTS.md](AGENTS.md)

## Current best

| Slice | Bytes | bpb | Commit |
|-------|-------|-----|--------|
| *reference:* record fx2-cmix-transformer (g++ build), enwik9_10M slice | 1,264,682 | 1.0117 | 44a7ea1 |
| *reference:* cmix-neif (g++ build), first 10M of its preprocessed stream | 1,918,894 | 1.535 | 5146837 |

Neither is our own work yet. Experiment backlog: [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md).

## Getting the data

```sh
scripts/fetch_data.sh   # downloads enwik9, derives enwik8 and slices into data/ (gitignored)
```
