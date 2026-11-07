// [sub100mil G1] Gated linear network (Veness et al. 2017/2021) as a final stage after the BITLSTM head.
// N neurons, each a geometric mixer (logit out = w . x) over a small set of stretched base predictions x,
// whose weight vector is selected by K half-space gates (sign(v_h . z - b_h), random fixed hyperplanes)
// over side information z (disagreement features). Each neuron is trained on its own log loss
// (w -= lr * (p - y) * x, clipped); the stage output is the mean of the neuron logits.
// Weights start as identity on the incoming (coded) prediction, so a fresh region changes nothing.
// Deterministic: fixed seed, fixed operation order, the same code in encoder and decoder.
#ifndef G1_GLN_H
#define G1_GLN_H
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace g1 {

constexpr int KX = 8;  // base inputs
constexpr int ZD = 8;  // side-information dimensions

struct Config {
  int n = 8, k = 8, seed = 923;
  float lr = 0.0006f, lrmin = 0.0001f, tau = 64, bscale = 0.3f, wclip = 8.f;
};

inline float Squash(float x) { return 1.f / (1.f + std::exp(-x)); }
inline float Stretch(float p) {
  if (p < 1e-6f) p = 1e-6f;
  if (p > 1 - 1e-6f) p = 1 - 1e-6f;
  return std::log(p / (1 - p));
}

// Feature extraction. in1: the final (layer-1) mixer's stretched inputs: [0, nmix) the layer-0 mixer outputs,
// then fxcm, the byte mixer (transformer), E2. m1: the final mixer's raw logit; psse: after SSE; sq: stretch of
// the coded probability (after the BITLSTM head); e1cb: E1's per-bit confidence bucket (0..15).
inline void Features(const float* in1, int nmix, float m1, float psse, float sq, int e1cb, float* X, float* Z) {
  float m = 0, mx = -99, mn = 99;
  for (int i = 0; i < nmix; ++i) {
    m += in1[i];
    if (in1[i] > mx) mx = in1[i];
    if (in1[i] < mn) mn = in1[i];
  }
  m /= nmix;
  const float fx = in1[nmix], tf = in1[nmix + 1], e2 = in1[nmix + 2];
  X[0] = fx; X[1] = tf; X[2] = e2; X[3] = m1; X[4] = m; X[5] = Stretch(psse); X[6] = sq; X[7] = 0.5f;
  Z[0] = sq * 0.25f; Z[1] = (tf - sq) * 0.5f; Z[2] = (fx - sq) * 0.5f; Z[3] = (mx - mn) * 0.25f;
  Z[4] = (m1 - sq) * 0.5f; Z[5] = (e2 - sq) * 0.5f; Z[6] = (m - sq) * 0.5f; Z[7] = (e1cb - 7.5f) * 0.2f;
}

class Gln {
 public:
  explicit Gln(const Config& c) : c_(c), nctx_(1 << c.k) {
    std::mt19937 rng(c.seed);
    std::normal_distribution<float> nd(0, 1);
    v_.resize((size_t)c.n * c.k * ZD);
    b_.resize((size_t)c.n * c.k);
    for (int j = 0; j < c.n; ++j)
      for (int h = 0; h < c.k; ++h) {
        float* v = &v_[((size_t)j * c.k + h) * ZD];
        float nn = 0;
        for (int i = 0; i < ZD; ++i) { v[i] = nd(rng); nn += v[i] * v[i]; }
        nn = std::sqrt(nn);
        for (int i = 0; i < ZD; ++i) v[i] /= nn;
        b_[(size_t)j * c.k + h] = nd(rng) * c.bscale;
      }
    w_.assign((size_t)c.n * nctx_ * KX, 0.f);
    for (size_t r = 0; r < (size_t)c.n * nctx_; ++r) w_[r * KX + 6] = 1.f;  // identity on the coded logit
    cnt_.assign((size_t)c.n * nctx_, 0);
    cur_.resize(c.n);
    out_.resize(c.n);
  }
  // Returns the stage's output logit for base inputs X and side information Z (kept for Update).
  float Predict(const float* X, const float* Z) {
    for (int i = 0; i < KX; ++i) x_[i] = X[i];
    float s = 0;
    for (int j = 0; j < c_.n; ++j) {
      int cx = 0;
      for (int h = 0; h < c_.k; ++h) {
        const float* v = &v_[((size_t)j * c_.k + h) * ZD];
        float d = 0;
        for (int i = 0; i < ZD; ++i) d += v[i] * Z[i];
        cx = cx * 2 + (d > b_[(size_t)j * c_.k + h]);
      }
      const size_t r = (size_t)j * nctx_ + cx;
      cur_[j] = r;
      const float* w = &w_[r * KX];
      float d = 0;
      for (int i = 0; i < KX; ++i) d += w[i] * x_[i];
      if (d > 12) d = 12;
      if (d < -12) d = -12;
      out_[j] = Squash(d);
      s += d;
    }
    return s / c_.n;
  }
  void Update(int bit) {
    for (int j = 0; j < c_.n; ++j) {
      const size_t r = cur_[j];
      float* w = &w_[r * KX];
      const uint32_t n = cnt_[r]++;
      const float l = c_.lrmin + (c_.lr - c_.lrmin) * c_.tau / (c_.tau + n);
      const float g = l * (out_[j] - bit);
      for (int i = 0; i < KX; ++i) {
        float t = w[i] - g * x_[i];
        if (t > c_.wclip) t = c_.wclip;
        if (t < -c_.wclip) t = -c_.wclip;
        w[i] = t;
      }
    }
  }

 private:
  Config c_;
  int nctx_;
  std::vector<float> v_, b_, w_, out_;
  std::vector<uint32_t> cnt_;
  std::vector<size_t> cur_;
  float x_[KX];
};

}  // namespace g1
#endif
