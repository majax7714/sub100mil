// a1_far: full-scale (586 MB coded stream) long-range repetition statistics for A1.
//
// For every coded position i and k in {16, 32, 64}: find the MOST RECENT earlier position whose
// preceding k bytes equal s[i-k..i-1] (exact, byte-verified; no hash-table eviction, because the
// previous occurrence is found by sorting (hash, position) pairs, one hash partition at a time),
// and record whether the byte that followed it equals s[i] ("correct") and how far back it is.
// Since the most recent occurrence is the closest one, its distance says whether ANY copy of the
// context lies inside the record's match windows (fxcm: 16 MiB buffer, cmix Match: 60e6 bytes).
//
// Output TSV: window, win_start, bytes, k, dist bucket, valid, correct.
// Memory: the stream + one partition of (hash, pos) pairs (~ 12 B x n / parts).
// Build: g++ -O2 -march=x86-64-v3 -o a1_far a1_far.cpp
// Usage: a1_far <stream> <out.tsv> [window_bytes=50000000] [parts=16]
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
static inline u64 mix64(u64 x) { x ^= x >> 31; x *= 0x7fb5d329728ea185ULL; x ^= x >> 27; x *= 0x81dadef4bc2dd44dULL; x ^= x >> 33; return x; }
static int dbucket(u64 d) {
  if (d < (1u << 20)) return 0; if (d < (16u << 20)) return 1; if (d < 60000000ULL) return 2;
  if (d < 200000000ULL) return 3; return 4;
}
static const char* DB[5] = {"<1M", "1M-16M", "16M-60M", "60M-200M", ">200M"};
struct HP { u64 h; u32 pos; } __attribute__((packed));

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: a1_far <stream> <out.tsv> [window] [parts]\n"); return 2; }
  u64 window = argc > 3 ? strtoull(argv[3], 0, 10) : 50000000ULL;
  int parts = argc > 4 ? atoi(argv[4]) : 16;
  FILE* f = fopen(argv[1], "rb"); if (!f) return 1;
  fseeko(f, 0, SEEK_END); u64 n = ftello(f); fseeko(f, 0, SEEK_SET);
  std::vector<u8> s(n); if (fread(s.data(), 1, n, f) != n) return 1; fclose(f);
  const u64 nwin = (n + window - 1) / window;
  const int K[3] = {16, 32, 64};
  std::vector<u64> agg(nwin * 3 * 5 * 2, 0);
  const u64 B = 0x100000001b3ULL;
  for (int ki = 0; ki < 3; ++ki) {
    int k = K[ki];
    u64 Bk = 1; for (int j = 0; j < k; ++j) Bk *= B;
    for (int part = 0; part < parts; ++part) {
      std::vector<HP> v; v.reserve(n / parts + n / (parts * 8) + 1024);
      u64 h = 0;
      for (u64 i = 0; i < n; ++i) {
        // h covers s[i-k..i-1] here (for i >= k): the context preceding position i
        if (i >= (u64)k) {
          u64 m = mix64(h);
          if ((int)(m % parts) == part) v.push_back(HP{m, (u32)i});
        }
        h = h * B + (s[i] + 1);
        if (i >= (u64)k) h -= (u64)(s[i - k] + 1) * Bk;
      }
      std::sort(v.begin(), v.end(), [](const HP& a, const HP& b) { return a.h != b.h ? a.h < b.h : a.pos < b.pos; });
      for (size_t t = 1; t < v.size(); ++t) {
        if (v[t].h != v[t - 1].h) continue;
        u64 i = v[t].pos, j = v[t - 1].pos;   // j < i: most recent earlier position with this context
        if (memcmp(&s[i - k], &s[j - k], k)) continue;  // hash collision: ignore
        u64* a = &agg[(((i / window) * 3 + ki) * 5 + dbucket(i - j)) * 2];
        a[0]++; if (s[j] == s[i]) a[1]++;
      }
      fprintf(stderr, "\rk=%d part %d/%d", k, part + 1, parts);
    }
  }
  fprintf(stderr, "\n");
  FILE* o = fopen(argv[2], "w");
  fprintf(o, "window\twin_start\tbytes\tk\tdist\tvalid\tcorrect\n");
  for (u64 w = 0; w < nwin; ++w) {
    u64 bytes = std::min(window, n - w * window);
    for (int ki = 0; ki < 3; ++ki)
      for (int d = 0; d < 5; ++d) {
        u64* a = &agg[((w * 3 + ki) * 5 + d) * 2];
        fprintf(o, "%llu\t%llu\t%llu\t%d\t%s\t%llu\t%llu\n", (unsigned long long)w, (unsigned long long)(w * window),
                (unsigned long long)bytes, K[ki], DB[d], (unsigned long long)a[0], (unsigned long long)a[1]);
      }
  }
  fclose(o);
  return 0;
}
