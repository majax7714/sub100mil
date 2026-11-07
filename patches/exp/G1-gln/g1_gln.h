// [sub100mil G1] Gated linear network (Veness et al. 2017/2021) as a final stage after the BITLSTM head.
// N neurons (k <= 8 gates each), each a geometric mixer (logit out = w . x) over a small set of stretched base predictions x,
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
#include <cstdlib>
#include <immintrin.h>

namespace g1 {

constexpr int KX = 8;  // base inputs
constexpr int ZD = 8;  // side-information dimensions

struct Config {
  int n = 8, k = 8, seed = 923;
  float lr = 0.001f, lrmin = 0.0003f, tau = 64, bscale = 0.15f, wclip = 8.f;
};

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

// Logistic over [-12, 12] by a 4097-knot table with linear interpolation (deterministic, no exp per call).
struct SquashTable {
  float t[4098];
  SquashTable() { for (int i = 0; i <= 4097; ++i) t[i] = 1.f / (1.f + std::exp(-((i - 2048) * (12.f / 2048)))); }
  float operator()(float x) const {
    if (x > 11.999f) x = 11.999f;
    if (x < -11.999f) x = -11.999f;
    const float u = (x + 12.f) * (2048.f / 12.f);
    const int i = (int)u;
    const float f = u - i;
    return t[i] + (t[i + 1] - t[i]) * f;
  }
};

// AVX2 implementation: up to 8 gates per neuron, one 8-wide register per weight row (KX = 8).
class Gln {
 public:
  explicit Gln(const Config& c) : c_(c), nctx_(1 << c.k), nh_(c.n * 8) {
    std::mt19937 rng(c.seed);
    std::normal_distribution<float> nd(0, 1);
    vt_ = (float*)aligned_alloc(32, sizeof(float) * ZD * nh_);
    b_ = (float*)aligned_alloc(32, sizeof(float) * nh_);
    for (int i = 0; i < ZD * nh_; ++i) vt_[i] = 0.f;
    for (int i = 0; i < nh_; ++i) b_[i] = 1e30f;  // unused gate slots never fire
    for (int j = 0; j < c.n; ++j)
      for (int h = 0; h < c.k; ++h) {
        float v[ZD], nn = 0;
        for (int i = 0; i < ZD; ++i) { v[i] = nd(rng); nn += v[i] * v[i]; }
        nn = std::sqrt(nn);
        for (int i = 0; i < ZD; ++i) vt_[i * nh_ + j * 8 + h] = v[i] / nn;  // transposed: [dim][gate]
        b_[j * 8 + h] = nd(rng) * c.bscale;
      }
    const size_t rows = (size_t)c.n * nctx_;
    w_ = (float*)aligned_alloc(32, sizeof(float) * KX * rows);
    for (size_t r = 0; r < rows; ++r)
      for (int i = 0; i < KX; ++i) w_[r * KX + i] = i == 6 ? 1.f : 0.f;  // identity on the coded logit
    cnt_.assign(rows, 0);
    cur_.resize(c.n);
    out_.resize(c.n);
  }
  ~Gln() { free(vt_); free(b_); free(w_); }
  Gln(const Gln&) = delete;
  Gln& operator=(const Gln&) = delete;
  float Squash(float x) const { return sq_(x); }
  // Returns the stage's output logit for base inputs X and side information Z (kept for Update).
  float Predict(const float* X, const float* Z) {
    x_ = _mm256_loadu_ps(X);
    float s = 0;
    for (int j = 0; j < c_.n; ++j) {
      __m256 d = _mm256_mul_ps(_mm256_load_ps(vt_ + j * 8), _mm256_set1_ps(Z[0]));
      for (int i = 1; i < ZD; ++i)
        d = _mm256_add_ps(d, _mm256_mul_ps(_mm256_load_ps(vt_ + i * nh_ + j * 8), _mm256_set1_ps(Z[i])));
      const int cx = _mm256_movemask_ps(_mm256_cmp_ps(d, _mm256_load_ps(b_ + j * 8), _CMP_GT_OQ)) & (nctx_ - 1);
      const size_t r = (size_t)j * nctx_ + cx;
      cur_[j] = r;
      __m256 p = _mm256_mul_ps(_mm256_load_ps(w_ + r * KX), x_);
      __m128 q = _mm_add_ps(_mm256_castps256_ps128(p), _mm256_extractf128_ps(p, 1));
      q = _mm_add_ps(q, _mm_movehl_ps(q, q));
      q = _mm_add_ss(q, _mm_shuffle_ps(q, q, 1));
      float dot = _mm_cvtss_f32(q);
      if (dot > 12) dot = 12;
      if (dot < -12) dot = -12;
      out_[j] = sq_(dot);
      s += dot;
    }
    return s / c_.n;
  }
  void Update(int bit) {
    const __m256 hi = _mm256_set1_ps(c_.wclip), lo = _mm256_set1_ps(-c_.wclip);
    for (int j = 0; j < c_.n; ++j) {
      const size_t r = cur_[j];
      const uint32_t n = cnt_[r]++;
      const float l = c_.lrmin + (c_.lr - c_.lrmin) * c_.tau / (c_.tau + n);
      const float g = l * (out_[j] - bit);
      __m256 w = _mm256_sub_ps(_mm256_load_ps(w_ + r * KX), _mm256_mul_ps(_mm256_set1_ps(g), x_));
      w = _mm256_max_ps(_mm256_min_ps(w, hi), lo);
      _mm256_store_ps(w_ + r * KX, w);
    }
  }

 private:
  Config c_;
  int nctx_, nh_;
  float *vt_, *b_, *w_;
  std::vector<float> out_;
  std::vector<uint32_t> cnt_;
  std::vector<size_t> cur_;
  __m256 x_;
  SquashTable sq_;
};

}  // namespace g1
#endif
