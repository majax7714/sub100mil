# order_rankdelta.py <new_article_order> <out>: re-code the article order (one id per line) as rank deltas, the format
# order_rankdelta.h decodes: line 1 = universe size M (max id + 1), then per article: its rank among the still-unused
# ids in [0, M) minus the rank at the previous article's position (the count of unused ids below it). The first
# article's value is its absolute rank. Same "%d\n" text, so cmix -c codes it like the original (about 19 KB smaller).
import sys
ids = [int(x) for x in open(sys.argv[1], 'rb').read().split()]
M = max(ids) + 1
fw = [0] * (M + 1)
for i in range(1, M + 1): fw[i] = i & -i  # all ids unused
def pre(i):  # unused ids in [0, i)
    s = 0
    while i > 0: s += fw[i]; i -= i & -i
    return s
def rem(i):
    i += 1
    while i <= M: fw[i] -= 1; i += i & -i
out, prev = [M], None
for x in ids:
    out.append(pre(x) - (pre(prev) if prev is not None else 0))
    rem(x); prev = x
open(sys.argv[2], 'w').write(''.join('%d\n' % v for v in out))
# verify with an independent decoder (sorted list of unused ids)
import bisect
un = list(range(M)); prev = None; dec = []
for d in out[1:]:
    r = (bisect.bisect_left(un, prev) if prev is not None else 0) + d
    x = un.pop(r); dec.append(x); prev = x
assert dec == ids
print(f'{len(ids)} ids, M={M}, verified')
