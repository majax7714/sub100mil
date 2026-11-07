// fullstream_check: run C3's two final stages (E12 lr 0.004, then X2) exactly as the baked build constructs them in
// -e / archive9 mode (coded block header on) over the real full-enwik9 coded stream, with a constant input p = 0.5.
// It is a crash / time / slot-count check of the specialists on the whole stream before a 60 h run (the metadata
// block where X2 works only arrives at the very end of a full run), NOT a size estimate (p_in is not the record's).
//
//   g++ -O2 -march=x86-64-v3 -std=c++17 -I patches/exp/E12-arith -I patches/exp/X2-timelaw \
//       -o build/exp/C3-full/fullstream_check patches/exp/C3-full/fullstream_check.cpp
//   build/exp/C3-full/fullstream_check build/exp/E12/full/e9.stream build/exp/C3-full/tree/dictionary/english.dic
// e9.stream = the -e coded stream (586,459,020 B, starts 07 37 af 12 70 07; see patches/exp/E12-arith/README.md).
#include <chrono>
#include <cstdio>
#include <vector>

#include "arith.h"
#include "x2_timelaw.h"

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: fullstream_check STREAM english.dic\n"); return 1; }
  FILE* f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  e12::E12Mix e12m(argv[2], true, 0.004);
  x2::TimeLawMix x2m(argv[2], true, 0.002);
  if (!e12m.spec.ok || !x2m.spec.ok) { fprintf(stderr, "cannot read dictionary\n"); return 1; }
  auto t0 = std::chrono::steady_clock::now();
  std::vector<unsigned char> buf(1 << 20);
  unsigned long long n = 0, x2_in = 0, changed = 0;
  size_t got;
  while ((got = fread(buf.data(), 1, buf.size(), f)) > 0) {
    for (size_t i = 0; i < got; i++, n++) {
      for (int b = 0; b < 8; b++) {
        int y = (buf[i] >> (7 - b)) & 1;
        float p = e12m.Predict(0.5f);
        int cls = -1;
        if (x2m.spec.InSlot(&cls)) x2_in++;
        float q = x2m.Predict(p);
        if (q != 0.5f) changed++;
        e12m.Update(y);
        x2m.Update(y);
      }
      if ((n & ((1ull << 26) - 1)) == 0 && n)
        fprintf(stderr, "%llu MB, %.0f s\n", n >> 20,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
  }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("coded bytes %llu, %.1f s (%.3f us/byte); bits with p changed by E12/X2: %llu; X2 in-slot bits %llu\n", n, s,
         1e6 * s / n, changed, x2_in);
  long e12_slots = 0;
  for (long v : e12m.spec.slots_opened) e12_slots += v;
  printf("E12 slots: %ld\n", e12_slots);
  printf("X2 slots: time %ld, rpid %ld\n", x2m.spec.slots_opened[x2::TIME], x2m.spec.slots_opened[x2::RPID]);
  return 0;
}
