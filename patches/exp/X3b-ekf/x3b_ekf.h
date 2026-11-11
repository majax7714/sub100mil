// [sub100mil X3b] Covariance-aware final combiner: online logistic regression by an extended Kalman filter
// (assumed-density filter / online Newton step) over the final stage's inputs, placed after G1.
//
// Every mixer in the record (and G1's neurons) learns by first-order SGD. The final stage's inputs are forecasts of
// the same bit and are ~0.99 correlated, so the SGD problem is badly conditioned: the weight directions that matter
// (contrasts between experts) get a tiny effective step. Sensor fusion handles correlated sources by carrying the
// posterior covariance P of the weights: gain = P x / (1 + h x'Px), h = p(1-p). A random-walk process noise q keeps
// the filter tracking (steady-state gain, no step-size schedule).
//
//   predict: s = w.x, p = sigma(s)
//   update:  Px = P x;  d = 1 + h x.Px;  w += Px (y - p) / d;  P -= (h / d) Px Px';  diag(P) += q
//
// Weights start as identity on input 0 (the incoming logit), P = p0 I, so the stage starts as a no-op.
// Deterministic: doubles, fixed operation order, the same code in encoder and decoder.
#ifndef X3B_EKF_H
#define X3B_EKF_H
#include <algorithm>
#include <cmath>
#include <cstring>

namespace x3b {

constexpr int K = 33;   // inputs: G1 logit, 23 layer-0 mixers, tf, fxcm, E2, final-mixer raw logit, st(psse), st(pfin),
                        // st(coded p before G1), median of the 23 mixers, bias 0.5

struct Ekf {
  double w[K], P[K][K], x[K], p = 0.5;
  double p0, q;
  explicit Ekf(double p0_ = 1e-3, double q_ = 1e-9) : p0(p0_), q(q_) {
    std::memset(w, 0, sizeof w); std::memset(P, 0, sizeof P); std::memset(x, 0, sizeof x);
    w[0] = 1.0;
    for (int i = 0; i < K; ++i) P[i][i] = p0;
  }
  // in1: the final mixer's 26 stretched inputs (23 layer-0 mixers, fxcm, transformer byte mixer, E2).
  // g: G1's output logit; m1: final mixer raw logit; spsse/spfin/sc: stretch of p after SSE, of the predictor's
  // final p, and of the coded p that entered G1.
  static void Features(const float* in1, float g, float m1, float spsse, float spfin, float sc, double* x) {
    float tmp[23];
    x[0] = g;
    for (int i = 0; i < 23; ++i) { x[1 + i] = in1[i]; tmp[i] = in1[i]; }
    x[24] = in1[24]; x[25] = in1[23]; x[26] = in1[25];
    x[27] = m1; x[28] = spsse; x[29] = spfin; x[30] = sc;
    std::nth_element(tmp, tmp + 11, tmp + 23);
    x[31] = tmp[11];
    x[32] = 0.5;
  }
  // Returns the output logit (clipped to +-12); x must be filled by Features.
  double Predict() {
    double s = 0;
    for (int i = 0; i < K; ++i) s += w[i] * x[i];
    s = s > 12 ? 12 : s < -12 ? -12 : s;
    p = 1 / (1 + std::exp(-s));
    return s;
  }
  void Update(int bit) {
    double Px[K];
    double s = 0;
    for (int i = 0; i < K; ++i) {
      double a = 0;
      for (int j = 0; j < K; ++j) a += P[i][j] * x[j];
      Px[i] = a;
      s += a * x[i];
    }
    const double h = p * (1 - p), den = 1 + h * s, e = (bit - p) / den, k = h / den;
    for (int i = 0; i < K; ++i) w[i] += Px[i] * e;
    for (int i = 0; i < K; ++i) {
      const double ki = k * Px[i];
      for (int j = 0; j < K; ++j) P[i][j] -= ki * Px[j];
    }
    for (int i = 0; i < K; ++i) P[i][i] += q;
  }
};

}  // namespace x3b
#endif
