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
| **C3 submission build (ours), full enwik9, round-trip OK: S = 100,800,236** as built (archive9 96,956,426 + unpacked comp9 3,843,810); **S = 99,886,519 with P1 packing** (comp9 3,377,271; derived; the clean unpacked and P1-packed builds give byte-identical archives on 1M and census 1M), −538,153 vs the record L = 100,424,672. Peak RSS 9,044.4 MiB | 96,956,426 | 0.7757 | 3e5366f |
| record + C4 (C3 + T1 interlanguage title copy + G1 GLN mixer, P1-packed) (ours), enwik9_10M slice; best that fits the budget | **1,260,281** | 1.0082 | dfc9a59 |
| record + E10 dynamic evaluation bk12q4d (ours), enwik9_10M slice; **over the time budget** (about 5.7 h, an estimate) | 1,261,985 | 1.0096 | 3caaaba |
| record + C3 (C1 + E2 LR ×0.5 + E12 arithmetic + X2 time law) (ours), enwik9_10M slice | 1,261,700 | 1.0094 | 971bfd6 |
| record + C2 (C1 + E2 LR ×0.5 + E12) (ours), census 10M slice `enwik9_mid10M_pa` (record 337,982, −4.20%) | 323,802 | 0.2590 | 14961a8 |
| record + C1 with E2 LR ×0.5 (ours), enwik9_10M slice | 1,261,970 | 1.0096 | 0f73794 |
| record + C1 (E1 + E2 + E3 + E9) (ours), enwik9_10M slice | 1,262,015 | 1.0096 | 0707157 |
| record + E12 arithmetic specialist (ours), census 10M slice `enwik9_mid10M_pa` (record 337,982 → 325,041, −3.83%) | 325,041 | 0.2600 | dd082f9 |
| record + E12 arithmetic specialist (ours), enwik9_10M slice (record 1,264,682) | 1,264,559 | 1.0116 | 4edce68 |
| record + E2 logit head (ours), enwik9_10M slice | 1,262,559 | 1.0100 | 1901d62 |
| **record + C2 (C1 + E2 LR ×0.5 + E12)** (ours), enwik9_100M slice | **11,980,914** | 0.9585 | 0425f65 |
| record + E2 logit head (ours), enwik9_100M slice | 11,988,349 | 0.9591 | 8fce6b5 |
| *reference:* record fx2-cmix-transformer (g++ build), enwik9_100M slice | 12,006,323 | 0.9605 | 65798d3 |
| *reference:* record fx2-cmix-transformer (g++ build), enwik9_10M slice | 1,264,682 | 1.0117 | 44a7ea1 |
| *reference:* cmix-neif (g++ build), first 10M of its preprocessed stream | 1,918,894 | 1.535 | 5146837 |

The references aren't our work. The first full enwik9 run (C3, 2026-11-11) round-trips with S 100,800,236 as built. The 99,886,519 figure swaps in P1's packed comp9 and decompressor, whose archives are byte-identical on slices; it is derived, not a separate full run. The prize needs S < 99,420,425. C4 is built (about 99.75M, an estimate). Batch 5 (2026-11-11) adds X3a, an order rule that cuts S by 95,257 B (measured on comp9, archive unchanged), and X3b, an EKF combiner (10M 1,259,519, about −35 KB estimated), for about 99.62M estimated. Its full run is parked: the next full run waits until the estimate clears the prize line. Experiment backlog: [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md).

## Getting the data

```sh
scripts/fetch_data.sh   # downloads enwik9, derives enwik8 and slices into data/ (gitignored)
```
