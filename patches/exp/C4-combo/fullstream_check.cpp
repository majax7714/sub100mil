// fullstream_check (C4): run C4's three dictionary final stages (E12 lr 0.004 [P1's regex-free E12], X2, T1 lr 0.004)
// exactly as the baked build constructs them in -e / archive9 mode (coded block header on) over the real
// full-enwik9 coded stream. Like patches/exp/C3-full/fullstream_check.cpp it is a crash / time / slot-count / RAM
// check, NOT a size estimate. Two additions:
//  - p_in is a deterministic pseudo-random probability per bit (not a constant 0.5), and the output probability
//    bit patterns of X2 and T1 are hashed, so two builds of this program can be compared exactly: built against
//    C4's tree (shared WRT decoder, wrt_dec.h) and against the original X2/T1 headers, the hashes must match.
//  - T1's slot/group/title counts.
//
// C4 build (the C4 tree's headers):
//   g++ -O2 -march=x86-64-v3 -std=c++17 -I build/exp/C4/tree/src/models -o build/exp/C4/fullstream_check \
//       patches/exp/C4-combo/fullstream_check.cpp
// Reference build (the original X2 and T1 headers, same E12):
//   mkdir -p build/exp/C4/fsref && cp build/exp/C4/tree/src/models/{e12_arith.h,e12_re.h} build/exp/C4/fsref/ &&
//   cp patches/exp/X2-timelaw/x2_timelaw.h patches/exp/T1-fieldcopy/t1_fieldcopy.h build/exp/C4/fsref/ &&
//   g++ -O2 -march=x86-64-v3 -std=c++17 -I build/exp/C4/fsref -o build/exp/C4/fullstream_check_ref \
//       patches/exp/C4-combo/fullstream_check.cpp
// Run: fullstream_check build/exp/E12/full/e9.stream <english.dic>   (under /usr/bin/time -v for peak RSS)
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "e12_arith.h"
#include "x2_timelaw.h"
#include "t1_fieldcopy.h"

static inline uint64_t mix64(uint64_t x) {
  x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
  return x;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: fullstream_check STREAM english.dic\n"); return 1; }
  FILE* f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  e12::E12Mix e12m(argv[2], true, 0.004);
  x2::TimeLawMix x2m(argv[2], true, 0.002);
  t1::FieldCopyMix t1m(argv[2], true, 0.004);
  if (!e12m.spec.ok || !x2m.spec.ok || !t1m.spec.ok) { fprintf(stderr, "cannot read dictionary\n"); return 1; }
  auto t0 = std::chrono::steady_clock::now();
  std::vector<unsigned char> buf(1 << 20);
  unsigned long long n = 0, x2_in = 0, t1_in = 0, x2_changed = 0, t1_changed = 0, bits = 0;
  uint64_t hx = 1469598103934665603ULL, ht = hx;
  size_t got;
  while ((got = fread(buf.data(), 1, buf.size(), f)) > 0) {
    for (size_t i = 0; i < got; i++, n++) {
      for (int b = 0; b < 8; b++, bits++) {
        int y = (buf[i] >> (7 - b)) & 1;
        const float pin = 0.02f + 0.96f * (float)(mix64(bits) >> 40) * (1.0f / 16777216.0f);
        float p = e12m.Predict(pin);
        int cls = -1;
        if (x2m.spec.InSlot(&cls)) x2_in++;
        float q = x2m.Predict(p);
        if (q != p) { x2_changed++; uint32_t u; memcpy(&u, &q, 4); hx = mix64(hx ^ u ^ (bits << 32)); }
        if (t1m.spec.InSlot(&cls)) t1_in++;
        float r = t1m.Predict(q);
        if (r != q) { t1_changed++; uint32_t u; memcpy(&u, &r, 4); ht = mix64(ht ^ u ^ (bits << 32)); }
        e12m.Update(y);
        x2m.Update(y);
        t1m.Update(y);
      }
      if ((n & ((1ull << 26) - 1)) == 0 && n)
        fprintf(stderr, "%llu MB, %.0f s\n", n >> 20,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
  }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("coded bytes %llu, %.1f s (%.3f us/byte)\n", n, s, 1e6 * s / n);
  long e12_slots = 0;
  for (long v : e12m.spec.slots_opened) e12_slots += v;
  printf("E12 slots: %ld\n", e12_slots);
  printf("X2 slots: time %ld, rpid %ld; in-slot bits %llu, changed %llu, hash %016llx\n", x2m.spec.slots_opened[x2::TIME],
         x2m.spec.slots_opened[x2::RPID], x2_in, x2_changed, (unsigned long long)hx);
  printf("T1 slots %ld, groups %ld, titles queued %ld, title links %ld; in-slot bits %llu, changed %llu, hash %016llx\n",
         t1m.spec.slots_opened, t1m.spec.groups_seen, t1m.spec.titles_queued, t1m.spec.eq_links, t1_in, t1_changed,
         (unsigned long long)ht);
  return 0;
}
