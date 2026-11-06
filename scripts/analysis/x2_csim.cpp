// X2: causal re-simulation of the real final stages on a per-byte cost dump: the record's per-bit probabilities,
// then (optionally) E12's final stage, then X2's time-law stage (patches/exp/X2-timelaw/x2_timelaw.h), exactly as
// the cmix patches chain them after SSE. Reports the coder's cost with and without each stage.
//
//   g++ -O2 -march=x86-64-v3 -std=c++17 -I patches/exp/E12-arith -I patches/exp/X2-timelaw
//       -o build/exp/X2/x2_csim scripts/analysis/x2_csim.cpp
//   build/exp/X2/x2_csim COSTLOG english.dic [e12=1] [x2=1] [seed=SEED.tsv]
// seed: rows "rev<TAB>T<TAB>pid" (T in the block's time units) of the full block's records BEFORE the proxy's first
// one, pre-loaded into the time law (what a full run would know; written by x2_meta2.py --seed-out).
// (the coder discretizes p as 1 + 65534 p over 65536; the same is done here)
#include <cstring>
#include <fstream>

#include "arith.h"
#include "x2_timelaw.h"

struct Rec {
  uint8_t byte, cm_len, cm_pred, fx_len, fx_pred, sm_len, sm_pred, flags;
  uint16_t pbit[8];
  float cost, cost_tf, cost_fx, cost_ppmd;
};
static_assert(sizeof(Rec) == 40, "rec");

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: x2_csim COSTLOG english.dic [e12=0|1] [x2=0|1]\n"); return 1; }
  int use_e12 = 0, use_x2 = 1;
  const char* seed = nullptr;
  for (int i = 3; i < argc; i++) {
    if (!strncmp(argv[i], "e12=", 4)) use_e12 = atoi(argv[i] + 4);
    if (!strncmp(argv[i], "x2=", 3)) use_x2 = atoi(argv[i] + 3);
    if (!strncmp(argv[i], "seed=", 5)) seed = argv[i] + 5;
  }
  std::ifstream f(argv[1], std::ios::binary);
  f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(16);
  size_t n = (sz - 16) / 40;
  std::vector<Rec> R(n);
  f.read((char*)R.data(), n * 40);
  std::unique_ptr<e12::E12Mix> e12m;
  std::unique_ptr<x2::TimeLawMix> x2m;
  if (use_e12) e12m.reset(new e12::E12Mix(argv[2], false, 0.002));
  if (use_x2) x2m.reset(new x2::TimeLawMix(argv[2], false, 0.002));
  if (seed && x2m) {
    FILE* sf = fopen(seed, "r");
    long long rv, T, pid; long k = 0;
    while (sf && fscanf(sf, "%lld %lld %lld", &rv, &T, &pid) == 3) { x2m->spec.Seed(rv, T, pid); k++; }
    if (sf) fclose(sf);
    fprintf(stderr, "seeded %ld records\n", k);
  }
  double rec = 0, after_e12 = 0, full = 0, in_x2_rec = 0, in_x2_full = 0;
  for (size_t i = 0; i < n; i++) {
    for (int b = 0; b < 8; b++) {
      int y = (R[i].byte >> (7 - b)) & 1;
      double pr = R[i].pbit[b] / 65536.0;
      float p = (float)pr;
      if (e12m) p = e12m->Predict(p);
      double pe = 1 + 65534.0 * p; pe = (int)pe / 65536.0;  // coder discretization
      if (!e12m) pe = pr;
      int cls = -1;
      bool in = x2m && x2m->spec.InSlot(&cls);
      if (x2m) p = x2m->Predict(p);
      double pf = 1 + 65534.0 * p; pf = (int)pf / 65536.0;
      if (!x2m) pf = pe;
      rec += -std::log2(y ? pr : 1 - pr);
      after_e12 += -std::log2(y ? pe : 1 - pe);
      full += -std::log2(y ? pf : 1 - pf);
      if (in) { in_x2_rec += -std::log2(y ? pe : 1 - pe); in_x2_full += -std::log2(y ? pf : 1 - pf); }
      if (e12m) e12m->Update(y);
      if (x2m) x2m->Update(y);
    }
  }
  printf("coded bytes %zu: record %.0f B, after E12 %.0f B (%+.0f), after X2 %.0f B (%+.0f vs E12 stage, %+.0f vs record)\n",
         n, rec / 8, after_e12 / 8, (after_e12 - rec) / 8, full / 8, (full - after_e12) / 8, (full - rec) / 8);
  if (x2m) printf("X2 slots: time %ld, rpid %ld; bits in X2 slots %.0f -> %.0f B\n", x2m->spec.slots_opened[x2::TIME],
                  x2m->spec.slots_opened[x2::RPID], in_x2_rec / 8, in_x2_full / 8);
  return 0;
}
