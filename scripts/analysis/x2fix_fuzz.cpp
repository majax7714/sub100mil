// x2fix: checked-build fuzz (libstdc++ bounds assertions + trapping signed overflow; no libasan on the box) of the X2 and T1 final stages on hostile metadata-like text (random page-id deltas incl.
// negative and 14-digit ones, over-long digit runs, records with and without "restrictions>", broken lines,
// interlanguage groups). Build twice, against C4's headers (reads the bitmap out of bounds) and C4-x2fix's (clean):
//   g++ -O1 -g -D_GLIBCXX_ASSERTIONS -ftrapv -march=x86-64-v3 -std=c++17 \
//       -I build/exp/x2fix/tree/src/models -o build/exp/x2fix/fuzz_new scripts/analysis/x2fix_fuzz.cpp
//   build/exp/x2fix/fuzz_new english.dic [seeds=200]
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include "x2_timelaw.h"
#include "t1_fieldcopy.h"

int main(int argc, char** argv) {
  if (argc < 2) return 1;
  int seeds = argc > 2 ? atoi(argv[2]) : 200;
  auto tabs = wrt::Load(argv[1]);
  if (!tabs->ok) { fprintf(stderr, "no dictionary\n"); return 1; }
  long slots = 0, bytes = 0;
  for (int sd = 0; sd < seeds; sd++) {
    std::mt19937_64 g(sd);
    auto R = [&](uint64_t n) { return (int64_t)(g() % n); };
    auto digits = [&](int n) { std::string s; for (int i = 0; i < n; i++) s += (char)('0' + R(10)); return s; };
    std::string t = "x\n";
    if (sd % 3 == 0) t = "  <page>\n    <title>Albedo (x)</title>\n      <text xml:space=\"preserve\">a\n  </page>\n" + t;
    int mode = sd % 5;
    for (int r = 0; r < 400; r++) {
      std::string d;
      switch (mode == 4 ? (int)R(4) : mode) {
        case 0: d = std::to_string(1 + R(64)); break;                       // id-order run (opens rpid slots)
        case 1: d = (R(2) ? "-" : "") + std::to_string(R(3000000)); break;  // jumps both ways
        case 2: d = "-" + digits(1 + (int)R(14)); break;                     // far below zero
        default: d = digits(1 + (int)R(14)); break;                         // huge
      }
      if (r % 7 == 3 && mode != 1) d = std::to_string(1 + R(3));
      t += ">" + d + "\n";
      if (R(5) == 0) t += "restrictions>move=:edit=\n";
      if (R(40) == 0) t += "junk>\n";
      t += "revision>\nid>" + (R(30) ? std::to_string(18100000 + R(40000000)) : digits(1 + (int)R(60))) + "\n";
      t += "timestamp>0" + (R(30) ? std::to_string(R(6)) + std::to_string(R(372)) + ":" + std::to_string(R(86400))
                                  : digits((int)R(40)) + ":" + digits((int)R(40))) + "\n";
      t += "contributor>\nip>1.2.3.4\n/contributor>\n";
    }
    for (int k = 0; k < 50; k++) t += std::string("[") + (R(9) ? "de" : "zh-min-nan") + ":" + (R(2) ? "Albedo" : "X") +
                                    "]" + (R(4) ? "\n" : "</text>\n");
    x2::TimeLawMix x(argv[1], sd & 1);
    t1::FieldCopyMix f(argv[1], sd & 1);
    float p = 0.5f;
    for (unsigned char ch : t) {
      uint8_t cb = R(500) ? tabs->enc[ch] : (uint8_t)R(256);  // now and then a raw byte (word codes, case marks)
      for (int b = 7; b >= 0; b--) {
        float q = f.Predict(x.Predict(p));
        if (!(q > 0 && q < 1)) { fprintf(stderr, "bad p\n"); return 2; }
        int y = (cb >> b) & 1;
        x.Update(y); f.Update(y);
      }
      bytes++;
    }
    slots += x.spec.slots_opened[0] + x.spec.slots_opened[1] + f.spec.slots_opened;
  }
  printf("ok: %d seeds, %ld bytes, %ld slots opened\n", seeds, bytes, slots);
  return 0;
}
