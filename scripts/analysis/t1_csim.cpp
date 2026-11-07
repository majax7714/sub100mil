// T1: causal re-simulation of the real final stage (patches/exp/T1-fieldcopy/t1_fieldcopy.h) on a per-byte cost
// dump: the record's per-bit probabilities, then T1's stage, as the cmix patch chains it (T1's slots never overlap
// E12's or X2's, so the stages are independent). Reports the coder's cost with and without the stage.
//
//   g++ -O2 -march=x86-64-v3 -std=c++17 -I patches/exp/T1-fieldcopy -o build/exp/T1/t1_csim scripts/analysis/t1_csim.cpp
//   build/exp/T1/t1_csim COSTLOG english.dic [lr=0.004] [seed=SEED.txt]   (seed: t1_seed.py)
//   build/exp/T1/t1_csim STREAM english.dic stream=1            (raw coded stream: counts and model time only)
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>

#include "t1_fieldcopy.h"

struct Rec {
  uint8_t byte, cm_len, cm_pred, fx_len, fx_pred, sm_len, sm_pred, flags;
  uint16_t pbit[8];
  float cost, cost_tf, cost_fx, cost_ppmd;
};
static_assert(sizeof(Rec) == 40, "rec");

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: t1_csim COSTLOG english.dic [lr=X]\n"); return 1; }
  double lr = 0.004;
  const char* seed = nullptr;
  for (int i = 3; i < argc; i++) {
    if (!strncmp(argv[i], "lr=", 3)) lr = atof(argv[i] + 3);
    if (!strncmp(argv[i], "seed=", 5)) seed = argv[i] + 5;
  }
  int stream = 0;
  for (int i = 3; i < argc; i++) if (!strncmp(argv[i], "stream=", 7)) stream = atoi(argv[i] + 7);
  if (stream) {  // raw coded stream (no costs): count titles/groups/slots and time the model alone
    FILE* sf = fopen(argv[1], "rb");
    std::unique_ptr<t1::FieldCopy> sp(new t1::FieldCopy(argv[2], false));
    std::vector<unsigned char> buf(1 << 20);
    size_t r, tot = 0;
    long bycls[t1::NCLS] = {0};
    auto t0 = std::chrono::steady_clock::now();
    while ((r = fread(buf.data(), 1, buf.size(), sf)) > 0) {
      for (size_t i = 0; i < r; i++) for (int b = 7; b >= 0; b--) {
        long before = sp->slots_opened;
        sp->Bit((buf[i] >> b) & 1);
        int c;
        if (sp->slots_opened != before && sp->InSlot(&c)) bycls[c]++;
      }
      tot += r;
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("stream %zu B: titles queued %ld, groups %ld, slots %ld, title links %ld; %.1f s (%.3f us/B)\n", tot,
           sp->titles_queued, sp->groups_seen, sp->slots_opened, sp->eq_links, s, 1e6 * s / tot);
    for (int c = 0; c < t1::NCLS; c++) printf("  class %2d: %ld slots\n", c, bycls[c]);
    return 0;
  }
  std::ifstream f(argv[1], std::ios::binary);
  f.seekg(0, std::ios::end); size_t sz = f.tellg(); f.seekg(16);
  size_t n = (sz - 16) / 40;
  std::vector<Rec> R(n);
  f.read((char*)R.data(), n * 40);
  std::unique_ptr<t1::FieldCopyMix> m(new t1::FieldCopyMix(argv[2], false, lr));
  if (!m->spec.ok) { fprintf(stderr, "cannot read dictionary\n"); return 1; }
  if (seed) {  // rows "lang eq n": the full run's per-language counts before the proxy's first group
    FILE* sf = fopen(seed, "r");
    char lang[64]; int eq, nn; long k = 0;
    while (sf && fscanf(sf, "%63s %d %d", lang, &eq, &nn) == 3) { m->spec.Seed(lang, eq, nn); k++; }
    if (sf) fclose(sf);
    fprintf(stderr, "seeded %ld languages\n", k);
  }
  double rec = 0, full = 0, cr[t1::NCLS] = {0}, cf[t1::NCLS] = {0};
  long inb = 0, ns[t1::NCLS] = {0};
  for (size_t i = 0; i < n; i++) {
    for (int b = 0; b < 8; b++) {
      int y = (R[i].byte >> (7 - b)) & 1;
      double pr = R[i].pbit[b] / 65536.0;
      float p = (float)pr;
      int cls = -1;
      long before = m->spec.slots_opened;
      bool in = m->spec.InSlot(&cls);
      p = m->Predict(p);
      double pf = 1 + 65534.0 * p; pf = (int)pf / 65536.0;
      if (!in) pf = pr;
      rec += -std::log2(y ? pr : 1 - pr);
      full += -std::log2(y ? pf : 1 - pf);
      if (in) { cr[cls] += -std::log2(y ? pr : 1 - pr); cf[cls] += -std::log2(y ? pf : 1 - pf); inb++; }
      m->Update(y);
      int c2;
      if (m->spec.slots_opened != before && m->spec.InSlot(&c2)) ns[c2]++;
    }
  }
  printf("coded bytes %zu: record %.0f B, with T1 %.0f B (%+.0f B)\n", n, rec / 8, full / 8, (full - rec) / 8);
  printf("titles queued %ld, groups %ld, slots %ld, title links %ld, in-slot bits %ld\n", m->spec.titles_queued,
         m->spec.groups_seen, m->spec.slots_opened, m->spec.eq_links, inb);
  for (int c = 0; c < t1::NCLS; c++)
    if (cr[c] > 0) printf("  class %2d: %ld slots, %.0f -> %.0f B (%+.0f), %+.3f bits/slot\n", c, ns[c], cr[c] / 8, cf[c] / 8,
                          (cf[c] - cr[c]) / 8, ns[c] ? (cf[c] - cr[c]) / ns[c] : 0.0);
  return 0;
}
