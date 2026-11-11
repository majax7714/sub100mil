// Isolated cost of the X3b EKF stage: ns per coded bit (Features + Predict + Update) on synthetic correlated inputs.
#include "x3b_ekf.h"
#include <chrono>
#include <cstdio>
#include <random>
int main() {
  x3b::Ekf e; std::mt19937 r(1); std::normal_distribution<float> nd(0, 1);
  const int N = 20000000; float in1[26]; double acc = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < N; ++i) {
    float b = nd(r) * 3; for (int j = 0; j < 26; ++j) in1[j] = b + 0.1f * ((i * 31 + j * 17) % 13 - 6);
    x3b::Ekf::Features(in1, b, b, b, b, b, e.x); acc += e.Predict(); e.Update(b + nd(r) > 0);
  }
  double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N;
  printf("%.1f ns per coded bit (%.2f us per coded byte), sizeof %zu B, acc %.3f\n", ns, ns * 8 / 1000, sizeof(x3b::Ekf), acc);
}
