"""Reader for FX2_COSTLOG per-byte cost logs (see README.md in this directory).

    import sys; sys.path.insert(0, 'patches/tools/per-byte-cost'); import costlog
    r = costlog.load('cost.bin')          # numpy structured array, one row per coded byte
    r['byte'], r['cost'], r['cost_tf'], r['pbit']  (pbit: coder's P(bit=1) * 65536, MSB first)

Run as a script for a summary:  python3 costlog.py cost.bin
"""
import sys
import numpy as np

MAGIC = b"FX2COST\0"
DTYPE = np.dtype([
    ("byte", "u1"),       # the coded (WRT-stream) byte
    ("cm_len", "u1"),     # cmix Match models: longest current match, in BITS (0..255)
    ("cm_pred", "u1"),    #   ... and that model's expected byte
    ("fx_len", "u1"),     # fxcm MatchModel2 best candidate length (rebased: bytes - 4; 0 = none)
    ("fx_pred", "u1"),    #   ... expected byte
    ("sm_len", "u1"),     # fxcm SparseMatchModel length (0 = none)
    ("sm_pred", "u1"),    #   ... expected byte
    ("flags", "u1"),      # bit0: fxcm best candidate in delta/recovery mode; bit1: transformer override
    ("pbit", "<u2", 8),   # final P(bit=1) as discretized by the coder (1 + 65534 p), MSB first
    ("cost", "<f4"),      # bits the record paid for this byte (from pbit, i.e. the coder's model)
    ("cost_tf", "<f4"),   # bits the transformer alone would pay (byte_mixer bit probabilities)
    ("cost_fx", "<f4"),   # bits fxcm's final output alone would pay
    ("cost_ppmd", "<f4"), # bits PPMD (order 25) alone would pay
])
assert DTYPE.itemsize == 40


def load(path, mmap=True):
    with open(path, "rb") as f:
        head = f.read(16)
    if head[:8] != MAGIC:
        raise ValueError(f"{path}: not an FX2_COSTLOG file")
    ver, recsz = np.frombuffer(head[8:16], "<u4")
    if ver != 1 or recsz != DTYPE.itemsize:
        raise ValueError(f"{path}: unsupported version {ver} / record size {recsz}")
    if mmap:
        return np.memmap(path, dtype=DTYPE, mode="r", offset=16)
    return np.fromfile(path, dtype=DTYPE, offset=16)


def summary(r):
    n = len(r)
    tot = float(r["cost"].astype(np.float64).sum())
    out = [f"coded bytes {n:,}", f"record: {tot:,.0f} bits = {tot / 8:,.0f} B ({tot / n:.4f} bits/coded byte)"]
    for k, name in (("cost_tf", "transformer alone"), ("cost_fx", "fxcm alone"), ("cost_ppmd", "ppmd alone")):
        t = float(r[k].astype(np.float64).sum())
        out.append(f"{name}: {t / 8:,.0f} B ({t / n:.4f} bits/coded byte)")
    return "\n".join(out)


if __name__ == "__main__":
    print(summary(load(sys.argv[1])))
