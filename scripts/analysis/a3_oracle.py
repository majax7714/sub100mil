#!/usr/bin/env python3
"""A3: per-block oracle switching between K independent E2 logit heads (upper bound).

Usage: a3_oracle.py LOSS.f32 m1,m2,... [--blocks 4096,16384,...] [--calib 0.59]

LOSS.f32 is `a3_sim indep` output: float32 loss[N][K] in nats, one column per LR multiplier.
For each block size B (in predicted tokens) it reports, in bytes of head loss (nats / ln2 / 8):
  - best fixed multiplier over the whole stream (no side info; that is plain tuning)
  - oracle: per block, the column with the lowest block loss (independent trajectories)
  - side info: log2(K) bits per block, and an adaptive cost (KT estimator, order-1 on the previous choice)
  - net oracle gain vs the default column (m=1) and vs the best fixed column
--mix also reports a decoder-side alternative that needs no side info but runs all K heads in the
decoder too: a fixed-share mixture of the K heads' token distributions (only p[y] is needed, from the losses).
--calib scales head-loss bytes to coded bytes (E2 10M: -2,123 B coded for -3,6xx B of head loss).
"""
import math
import sys

import numpy as np


def adaptive_bits(ch, K):
    """Order-1 KT (add-1/2) adaptive code length of a choice sequence, in bits."""
    cnt = np.full((K, K), 0.5)
    bits = 0.0
    prev = 0
    for c in ch:
        row = cnt[prev]
        bits -= math.log2(row[c] / row.sum())
        row[c] += 1
        prev = c
    return bits


def fixed_share(L, alpha):
    """Fixed-share (alpha) mixture of K experts; L = per-token losses [N][K] in nats. Returns total nats."""
    N, K = L.shape
    P = np.exp(-L)
    w = np.full(K, 1.0 / K)
    tot = 0.0
    for t in range(N):
        pw = w * P[t]
        s = pw.sum()
        tot -= math.log(s)
        w = (1 - alpha) * pw / s + alpha / K
    return tot


def main():
    a = sys.argv[1:]
    path, ms = a[0], [float(x) for x in a[1].split(',')]
    blocks = [4096, 16384, 65536, 262144, 1048576]
    calib = 0.59
    if '--blocks' in a:
        blocks = [int(x) for x in a[a.index('--blocks') + 1].split(',')]
    if '--calib' in a:
        calib = float(a[a.index('--calib') + 1])
    K = len(ms)
    L = np.fromfile(path, dtype=np.float32).reshape(-1, K).astype(np.float64)
    N = L.shape[0]
    tob = 1.0 / math.log(2) / 8.0
    tot = L.sum(0)
    i1 = ms.index(1.0) if 1.0 in ms else 0
    kb = int(np.argmin(tot))
    print(f"N={N} tokens, K={K}, multipliers {ms}")
    for k in range(K):
        print(f"  m={ms[k]:<6g} mean {tot[k] / N:.6f} nats/token  vs m=1: {(tot[k] - tot[i1]) * tob:+10.1f} B")
    print(f"best fixed: m={ms[kb]} ({(tot[kb] - tot[i1]) * tob:+.1f} B head loss vs m=1)")
    if '--mix' in a:
        for alpha in (1e-4, 1e-3):
            fs = fixed_share(L, alpha)
            print(f"fixed-share mixture (alpha {alpha:g}, all K heads in the decoder): "
                  f"{(fs - tot[i1]) * tob:+.1f} B vs m=1, {(fs - tot[kb]) * tob:+.1f} B vs best fixed")
    print(f"{'B':>8} {'blocks':>7} {'oracle-m1 B':>12} {'oracle-best B':>13} {'side naive B':>12} {'side adapt B':>12}"
          f" {'net vs m1':>10} {'net vs best':>11} {'coded (x{:.2f}) vs best'.format(calib):>24}")
    for B in blocks:
        nb = (N + B - 1) // B
        pad = nb * B - N
        Lp = np.vstack([L, np.zeros((pad, K))]) if pad else L
        bl = Lp.reshape(nb, B, K).sum(1)
        ch = bl.argmin(1)
        orc = bl.min(1).sum()
        naive = nb * math.log2(K) / 8
        adapt = adaptive_bits(ch, K) / 8
        side = min(naive, adapt)
        g1 = (orc - tot[i1]) * tob
        gb = (orc - tot[kb]) * tob
        print(f"{B:>8} {nb:>7} {g1:>12.1f} {gb:>13.1f} {naive:>12.1f} {adapt:>12.1f} {g1 + side:>10.1f} {gb + side:>11.1f}"
              f" {(gb + side) * calib:>24.1f}")
        hist = np.bincount(ch, minlength=K)
        print(f"{'':>8} choices: " + ' '.join(f"{ms[k]:g}:{hist[k]}" for k in range(K)))


if __name__ == '__main__':
    main()
