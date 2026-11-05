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
| none yet | | | |

## Getting the data

```sh
scripts/fetch_data.sh   # downloads enwik9, derives enwik8 and slices into data/ (gitignored)
```
