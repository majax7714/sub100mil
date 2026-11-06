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
| record + E10 dynamic evaluation bk12q4d (ours), enwik9_10M slice; **over the time budget** (about 5.7 h, an estimate) | 1,261,985 | 1.0096 | 3caaaba |
| **record + C1 (E1 + E2 + E3 + E9)** (ours), enwik9_10M slice; best that fits the budget | **1,262,015** | 1.0096 | 0707157 |
| record + E2 logit head (ours), enwik9_10M slice | 1,262,559 | 1.0100 | 1901d62 |
| *reference:* record fx2-cmix-transformer (g++ build), enwik9_10M slice | 1,264,682 | 1.0117 | 44a7ea1 |
| *reference:* cmix-neif (g++ build), first 10M of its preprocessed stream | 1,918,894 | 1.535 | 5146837 |

The references aren't our work. E2 is our first gain over the record: −2,123 B (−0.168%) on the 10M slice, not yet confirmed at full scale. Experiment backlog: [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md).

## Getting the data

```sh
scripts/fetch_data.sh   # downloads enwik9, derives enwik8 and slices into data/ (gitignored)
```
