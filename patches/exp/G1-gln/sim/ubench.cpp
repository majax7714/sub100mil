#include "g1_gln.h"
#include <cstdio>
#include <chrono>
int main(int argc, char** argv) {
  g1::Config c; if (argc > 1) c.n = atoi(argv[1]); if (argc > 2) c.k = atoi(argv[2]);
  g1::Gln g(c); std::mt19937 r(1); std::normal_distribution<float> nd(0, 2);
  const int M = 4096; std::vector<float> in(M * 26); for (auto& v : in) v = nd(r);
  float X[8], Z[8]; double acc = 0; long N = 20000000;
  auto t0 = std::chrono::steady_clock::now();
  for (long i = 0; i < N; ++i) {
    const float* f = &in[(i % M) * 26];
    g1::Features(f, 23, f[3], 0.3f, f[5], (int)(i & 15), X, Z);
    float p = g.Squash(g.Predict(X, Z)); acc += p; g.Update(i & 1);
  }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("n=%d k=%d: %.1f ns/bit, %.2f us/coded byte (acc %g)\n", c.n, c.k, s / N * 1e9, s / N * 8e6, acc);
}
