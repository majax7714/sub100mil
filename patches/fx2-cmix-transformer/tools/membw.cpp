// membw: STREAM-style single-core sequential read bandwidth vs working-set size (AVX2), and a
// "stream forever" mode used as a co-runner for cache/bandwidth contention tests.
//   membw sweep                 -> table of GB/s and bytes per TSC tick for 16 KiB .. 512 MiB
//   membw stream <MiB> <secs>   -> stream a buffer of that size for that long (co-runner)
// Build: g++ -O2 -march=x86-64-v3 membw.cpp -o membw   (pin with taskset -c N)
#include <immintrin.h>
#include <x86intrin.h>
#include <sys/mman.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <initializer_list>

static uint8_t* Alloc(size_t n) {
  void* p = nullptr;
  if (posix_memalign(&p, 1 << 21, n)) exit(1);
  madvise(p, n, MADV_HUGEPAGE);
  memset(p, 1, n);
  return (uint8_t*)p;
}

// 4 independent 32B loads per iteration (128 B), OR-reduced: the load ports, not ALUs, limit.
static __attribute__((noinline)) __m256i ReadPass(const uint8_t* p, size_t n) {
  __m256i a0 = _mm256_setzero_si256(), a1 = a0, a2 = a0, a3 = a0;
  for (size_t i = 0; i < n; i += 128) {
    a0 = _mm256_or_si256(a0, _mm256_load_si256((const __m256i*)(p + i)));
    a1 = _mm256_or_si256(a1, _mm256_load_si256((const __m256i*)(p + i + 32)));
    a2 = _mm256_or_si256(a2, _mm256_load_si256((const __m256i*)(p + i + 64)));
    a3 = _mm256_or_si256(a3, _mm256_load_si256((const __m256i*)(p + i + 96)));
  }
  return _mm256_or_si256(_mm256_or_si256(a0, a1), _mm256_or_si256(a2, a3));
}

// vpmaddubsw consumer (int8 GEMV weight-streaming mimic): 1 madd + 1 add per 32 B.
static __attribute__((noinline)) __m256i MaddPass(const uint8_t* p, size_t n) {
  const __m256i act = _mm256_set1_epi8(3);
  __m256i a0 = _mm256_setzero_si256(), a1 = a0, a2 = a0, a3 = a0;
  for (size_t i = 0; i < n; i += 128) {
    a0 = _mm256_add_epi16(a0, _mm256_maddubs_epi16(act, _mm256_load_si256((const __m256i*)(p + i))));
    a1 = _mm256_add_epi16(a1, _mm256_maddubs_epi16(act, _mm256_load_si256((const __m256i*)(p + i + 32))));
    a2 = _mm256_add_epi16(a2, _mm256_maddubs_epi16(act, _mm256_load_si256((const __m256i*)(p + i + 64))));
    a3 = _mm256_add_epi16(a3, _mm256_maddubs_epi16(act, _mm256_load_si256((const __m256i*)(p + i + 96))));
  }
  return _mm256_or_si256(_mm256_or_si256(a0, a1), _mm256_or_si256(a2, a3));
}

volatile int g_sink;
static void Sink(__m256i v) { g_sink += _mm256_extract_epi32(v, 0); }

int main(int argc, char** argv) {
  using clk = std::chrono::steady_clock;
  if (argc == 4 && !strcmp(argv[1], "stream")) {
    size_t n = (size_t)atof(argv[2]) * (1 << 20);
    double secs = atof(argv[3]);
    uint8_t* p = Alloc(n);
    auto t0 = clk::now();
    double bytes = 0;
    while (std::chrono::duration<double>(clk::now() - t0).count() < secs) {
      for (int r = 0; r < 16; ++r) { Sink(ReadPass(p, n)); bytes += n; }
    }
    double s = std::chrono::duration<double>(clk::now() - t0).count();
    fprintf(stderr, "streamer %.0f MiB: %.1f GB/s over %.1f s\n", n / 1048576.0, bytes / s / 1e9, s);
    return 0;
  }
  printf("%10s %12s %12s %12s %12s\n", "size_KiB", "read_GB/s", "read_B/tsc", "madd_GB/s", "madd_B/tsc");
  for (size_t kib = 16; kib <= 512 * 1024; kib *= 2) {
    for (size_t extra : {(size_t)0, kib / 2}) {
      size_t sz = (kib + extra) * 1024;
      if (extra && kib >= 512 * 1024) continue;
      uint8_t* p = Alloc(sz);
      size_t reps = (size_t)(4e9 / sz) + 1;
      double best[2] = {0, 0}, bestt[2] = {0, 0};
      for (int mode = 0; mode < 2; ++mode) {
        for (int trial = 0; trial < 5; ++trial) {
          Sink(mode ? MaddPass(p, sz) : ReadPass(p, sz));  // warm
          auto t0 = clk::now();
          uint64_t c0 = __rdtsc();
          for (size_t r = 0; r < reps / 5 + 1; ++r) Sink(mode ? MaddPass(p, sz) : ReadPass(p, sz));
          uint64_t c1 = __rdtsc();
          double s = std::chrono::duration<double>(clk::now() - t0).count();
          double b = (double)sz * (reps / 5 + 1);
          if (b / s > best[mode]) { best[mode] = b / s; bestt[mode] = b / (c1 - c0); }
        }
      }
      printf("%10zu %12.1f %12.2f %12.1f %12.2f\n", kib + extra, best[0] / 1e9, bestt[0], best[1] / 1e9, bestt[1]);
      fflush(stdout);
      free(p);
    }
  }
  return 0;
}
