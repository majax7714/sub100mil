# scripts/

All scripts locate the repo root from their own path, so they can be run from anywhere.
They need only bash, coreutils, GNU `time` (`/usr/bin/time`) and, for `fetch_data.sh`,
`curl` and `unzip`.

## fetch_data.sh

Downloads enwik9, derives the working slices and verifies checksums. It's idempotent:
a re-run skips the download if `data/raw/enwik9.zip` is present and intact, rebuilds
only missing or wrong-size outputs, and re-verifies everything.

```
scripts/fetch_data.sh            # fetch/build and verify against data/CHECKSUMS.sha256
scripts/fetch_data.sh --rehash   # regenerate data/CHECKSUMS.sha256 instead of verifying
```

Produces (all gitignored under `data/`):

| path | contents |
|---|---|
| `data/raw/enwik9.zip`, `data/raw/enwik9` | the corpus (10^9 bytes, md5 checked) |
| `data/slices/enwik9_1M` | first 10^6 bytes (smoke tests) |
| `data/slices/enwik9_10M` | first 10^7 bytes (main iteration benchmark) |
| `data/slices/enwik9_mid10M` | 10^7 bytes from offset 5×10^8 (overfitting guard) |
| `data/slices/enwik9_100M` | first 10^8 bytes, i.e. enwik8 (md5 checked); `enwik8` is a symlink to it |
| `data/CHECKSUMS.sha256` | sha256 of everything above |

## bench.sh

The standard harness every experiment goes through: compress, decompress, verify
byte-exact round-trip, print a summary, and append a row to `results/ledger.tsv`.

```
scripts/bench.sh --method NAME --slice PATH --comp 'CMD' --decomp 'CMD' \
                 [--threads N] [--notes TEXT] [--keep] [--no-ledger] \
                 [--decomp-size BYTES] [--extra-bytes N]
```

- `--comp` / `--decomp` are command templates with `{in}` and `{out}` placeholders.
  They run via `bash -c` from your current directory, so redirects and pipes work.
  For comp, `{in}` is the slice and `{out}` is `<workdir>/comp.bin`; for decomp, `{in}` is
  `comp.bin` and `{out}` is `<workdir>/decomp.out`. Paths are shell-quoted on substitution,
  so don't add your own quotes around `{in}`/`{out}`.
- `--method` is a free-form name for the ledger (e.g. `xz -9e -T1`, `cm-v3 order2+word`).
- `--threads` (default 1) is only recorded; it's up to the command to honor it.
- `--notes` goes in the ledger's notes column.
- `--decomp-size BYTES` / `--extra-bytes N` are appended to the notes
  (`decomp_size=...`, `extra_bytes=...`) and the summary prints total
  S = output + decomp_size + extra_bytes. `output_bytes` is always the compressed file size.
- `--keep` keeps the work dir (path printed at the end); otherwise it's deleted.
- `--no-ledger` skips the ledger row (use it for dry runs and harness tests, never for real results).
- Work dirs are `mktemp -d` under `$BENCH_TMP`, which defaults to the session scratchpad
  (`/tmp/claude-1000/...`). Point `BENCH_TMP` at a disk with room for full enwik9 runs.

What it records, per AGENTS.md "Measuring":

| ledger column | meaning |
|---|---|
| `date` | `date +%F` |
| `commit` | `git rev-parse --short HEAD`, plus `+dirty` if tracked files are modified (untracked files are ignored). Commit your code before a real run. |
| `slice` | basename of `--slice` |
| `input_bytes`, `output_bytes` | slice size, compressed file size |
| `bpb` | 8 × output / input, 4 decimals |
| `comp_s`, `decomp_s` | wall seconds from `/usr/bin/time -v` |
| `peak_rss_mb` | max of comp and decomp peak RSS, in MiB (kB / 1024) |
| `threads` | from `--threads` |
| `roundtrip_ok` | `yes` only if both steps exited 0 and `cmp` matched |
| `notes` | `--notes`, then `decomp_size=`/`extra_bytes=`, then `FAIL: <reason>` on failure |

A failed step or round-trip still writes a row (`roundtrip_ok=no`, reason in notes), since
failures are data too. If comp fails, decomp isn't run. Exit status: 0 on a verified
round-trip, 1 on any failure, 2 on usage errors.

### Examples

```
# baseline on the smoke-test slice
scripts/bench.sh --method 'xz -9e -T1' --slice data/slices/enwik9_1M \
  --comp 'xz -9e -T1 -c {in} > {out}' --decomp 'xz -d -c {in} > {out}'

# our compressor, counting the decompressor size toward S
scripts/bench.sh --method 'cm v1' --slice data/slices/enwik9_10M \
  --comp './build/cm c {in} {out}' --decomp './build/cm d {in} {out}' \
  --decomp-size "$(stat -c %s build/cm.zip)" --notes 'order-2 + word model'

# full run on a big scratch disk, keep the outputs
BENCH_TMP=/mnt/scratch scripts/bench.sh --method 'cm v1' --slice data/raw/enwik9 \
  --comp './build/cm c {in} {out}' --decomp './build/cm d {in} {out}' --keep
```
