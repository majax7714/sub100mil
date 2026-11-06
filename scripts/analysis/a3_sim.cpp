// A3 encoder-side selection: offline simulator for the E2 online logit head.
//
// Input: a dump written by the A3 dump build (patches/exp/A3-encoder-select, A3_DUMP=path):
//   per predicted token: int32 target y, float xn[192] (normalized final hidden state),
//   float raw_logits[205] (static head, pre-softcap).
// The head is E2's logit head, re-implemented with the same op order:
//   logits = raw + A^T [xn; 1]; p = softmax(15 tanh(logits/15)) (glue.cpp's head_softcap_softmax);
//   A -= lr * [xn; 1] (x) dl, dl = (p - onehot(y)) (1 - (l/15)^2), lr = m * sched(t) / (D + 2),
//   sched(t) = af + (a0 - af) / (1 + t / atau)  (E2 defaults a0 0.5, af 0.1, atau 8000).
// Losses are -ln p[y] in nats (float probabilities, no fp16 rounding).
//
// Modes:
//   a3_sim indep DUMP OUT.f32 m1,m2,...       K independent heads with LR multipliers m_k over
//                                              the whole stream; writes float loss[N][K]
//   a3_sim fork  DUMP OUT.u8  B m1,m2,...      fork-and-commit: at each block of B tokens fork the
//                                              shared state, run the block once per m_k, keep the
//                                              best (lowest block loss), commit its state; writes
//                                              the choice per block and prints the total loss
//   a3_sim fixed DUMP OUT.u8  B m1,m2,...      replay: block j uses multiplier index c[j] read from
//                                              OUT.u8 (a choice file) -> total loss (checks fork)
//   a3_sim forkla DUMP OUT.u8 B m1,m2,... H MDEF  fork-and-commit with lookahead: variant k is scored by
//                                              its block loss plus the loss of the next H blocks run
//                                              from its state with the default multiplier MDEF (the
//                                              greedy one-block score is biased toward low LRs, since
//                                              learning pays later); only block j's state is committed
// Env: A3_A0, A3_AF, A3_ATAU override the schedule.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "glue.h"

namespace {
constexpr int D = 192, V = 205, VP = 208;
constexpr size_t REC = 4 + 4 * D + 4 * V;

struct Dump {
  const uint8_t* base = nullptr;
  size_t n = 0;
  int y(size_t i) const { int32_t v; std::memcpy(&v, base + i * REC, 4); return v; }
  const float* xn(size_t i) const { return reinterpret_cast<const float*>(base + i * REC + 4); }
  const float* raw(size_t i) const { return reinterpret_cast<const float*>(base + i * REC + 4 + 4 * D); }
};

Dump open_dump(const char* path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) { perror(path); exit(1); }
  struct stat st; fstat(fd, &st);
  if (st.st_size % REC) { fprintf(stderr, "dump size %lld not a multiple of %zu\n", (long long)st.st_size, REC); exit(1); }
  Dump d;
  d.n = st.st_size / REC;
  d.base = static_cast<const uint8_t*>(mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
  madvise((void*)d.base, st.st_size, MADV_SEQUENTIAL);
  return d;
}

double A0 = 0.5, AF = 0.1, ATAU = 8000.0;

struct Head {
  std::vector<float> A = std::vector<float>(size_t(D + 1) * VP, 0.0f);
  // one predict+update step; returns -ln p[y]
  double step(const float* xn, const float* raw, int y, uint64_t t, float m) {
    alignas(64) float l[VP], p[VP];
    std::memcpy(l, raw, sizeof(float) * V);
    l[205] = l[206] = l[207] = 0.0f;
    for (int i = 0; i < D; i++) {
      const float a = xn[i];
      const float* row = A.data() + size_t(i) * VP;
      for (int o = 0; o < VP; o++) l[o] += row[o] * a;
    }
    const float* bias = A.data() + size_t(D) * VP;
    for (int o = 0; o < VP; o++) l[o] += bias[o];
    fx2::opt::head_softcap_softmax(l, p);
    double py = p[y];
    if (!(py > 1e-38)) py = 1e-38;
    alignas(64) float dl[VP];
    for (int o = 0; o < V; o++) {
      const float d = p[o] - (o == y ? 1.0f : 0.0f);
      const float c = l[o] * (1.0f / 15.0f);
      dl[o] = d * (1.0f - c * c);
    }
    dl[205] = dl[206] = dl[207] = 0.0f;
    const float lr = float(AF + (A0 - AF) / (1.0 + double(t) / ATAU)) * m * (1.0f / float(D + 2));
    float* Aw = A.data();
    for (int i = 0; i < D; i++) {
      const float c = lr * xn[i];
      float* row = Aw + size_t(i) * VP;
      for (int o = 0; o < VP; o++) row[o] -= c * dl[o];
    }
    float* bw = Aw + size_t(D) * VP;
    for (int o = 0; o < VP; o++) bw[o] -= lr * dl[o];
    return -std::log(py);
  }
};

std::vector<float> parse_list(const char* s) {
  std::vector<float> v;
  for (const char* p = s; *p;) {
    char* e; v.push_back(strtof(p, &e)); p = e; if (*p == ',') p++;
  }
  return v;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: see header\n"); return 2; }
  if (const char* e = getenv("A3_A0")) A0 = atof(e);
  if (const char* e = getenv("A3_AF")) AF = atof(e);
  if (const char* e = getenv("A3_ATAU")) ATAU = atof(e);
  const std::string mode = argv[1];
  Dump d = open_dump(argv[2]);
  const char* out = argv[3];
  fprintf(stderr, "%zu tokens, a0 %g af %g atau %g\n", d.n, A0, AF, ATAU);
  if (mode == "indep") {
    std::vector<float> ms = parse_list(argv[4]);
    const int K = ms.size();
    std::vector<Head> h(K);
    std::vector<float> loss(d.n * K);
    std::vector<double> tot(K, 0.0);
    for (size_t i = 0; i < d.n; i++)
      for (int k = 0; k < K; k++) {
        const double l = h[k].step(d.xn(i), d.raw(i), d.y(i), i, ms[k]);
        loss[i * K + k] = float(l);
        tot[k] += l;
      }
    FILE* f = fopen(out, "wb"); fwrite(loss.data(), 4, loss.size(), f); fclose(f);
    for (int k = 0; k < K; k++)
      printf("indep m=%g total %.3f nats mean %.6f\n", ms[k], tot[k], tot[k] / d.n);
    return 0;
  }
  const size_t B = strtoull(argv[4], nullptr, 10);
  std::vector<float> ms = parse_list(argv[5]);
  const int K = ms.size();
  const size_t nb = (d.n + B - 1) / B;
  std::vector<uint8_t> choice(nb, 0);
  if (mode == "fixed") {
    FILE* f = fopen(out, "rb"); if (fread(choice.data(), 1, nb, f) != nb) { fprintf(stderr, "short choice file\n"); return 1; } fclose(f);
  }
  const int H = mode == "forkla" ? atoi(argv[6]) : 0;
  const float mdef = mode == "forkla" ? strtof(argv[7], nullptr) : 1.0f;
  Head cur, trial, best, look;
  double total = 0;
  std::vector<double> perk(K, 0.0);
  std::vector<size_t> cnt(K, 0);
  for (size_t b = 0; b < nb; b++) {
    const size_t i0 = b * B, i1 = std::min(d.n, i0 + B);
    if (mode == "fixed") {
      const float m = ms[choice[b]];
      for (size_t i = i0; i < i1; i++) total += cur.step(d.xn(i), d.raw(i), d.y(i), i, m);
      continue;
    }
    double bl = 1e300, bs = 1e300; int bk = 0;
    for (int k = 0; k < K; k++) {
      trial.A = cur.A;
      double l = 0;
      for (size_t i = i0; i < i1; i++) l += trial.step(d.xn(i), d.raw(i), d.y(i), i, ms[k]);
      perk[k] += l;  // loss of variant k from the committed state, this block only
      double score = l;
      if (H > 0) {
        look.A = trial.A;
        const size_t j1 = std::min(d.n, i1 + H * B);
        for (size_t i = i1; i < j1; i++) score += look.step(d.xn(i), d.raw(i), d.y(i), i, mdef);
      }
      if (score < bs) { bs = score; bl = l; bk = k; best.A.swap(trial.A); }
    }
    cur.A.swap(best.A);
    choice[b] = uint8_t(bk);
    cnt[bk]++;
    total += bl;
  }
  if (mode == "fork" || mode == "forkla") {
    FILE* f = fopen(out, "wb"); fwrite(choice.data(), 1, nb, f); fclose(f);
    printf("%s H=%d B=%zu K=%d blocks %zu total %.3f nats mean %.6f\n", mode.c_str(), H, B, K, nb, total, total / d.n);
    for (int k = 0; k < K; k++)
      printf("  m=%g chosen %zu  (sum of its block losses from the committed state %.3f)\n", ms[k], cnt[k], perk[k]);
  } else {
    printf("fixed B=%zu total %.3f nats mean %.6f\n", B, total, total / d.n);
  }
  return 0;
}
