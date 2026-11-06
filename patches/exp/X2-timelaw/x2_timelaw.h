// X2: metadata-block time law ("radiometric dating") and redirect page-id law, as a final stage after E12.
//
// phda9 moves every page's revision header into one block after the last page, one record per page:
//   ">DELTA\nrevision>\n[restrictions>...\n]id>REVID\ntimestamp>0YD:S\ncontributor>\n(username>U\nid>UID|ip>IP)\n/contributor>\n"
// DELTA = page id - previous record's page id; the timestamp is "0" + (year - 2001) + (31 (month - 1) + day - 1),
// ":" + seconds of the day (UTC), unpadded.
//
// Law 1 (time): Wikipedia issues revision ids in time order. For a revision id >= 18.1M (below that, the 2005 import
// renumbered old revisions and their times are scrambled), the time is interpolated linearly between the nearest
// known revision ids below and above (all earlier records of the block, ids >= 18.1M). Candidates are the integer
// times within +-min(2000, max(10, 8 b)) of the interpolation, clipped to the two neighbours' times, weighted
// exp(-|T - t^| / b); b is a running mean of |error| (capped at 30000) per log2(neighbour gap + 1) bucket, initial 8.
// Law 2 (redirect page id): the full run's redirects follow the reordered articles in id order, so a page id is most
// likely one of the next ids after the previous record's that no earlier record used: candidates are the next 64
// unused ids as deltas, weighted (1 + j)^-1.5. The slot opens only after 4 consecutive records with deltas in
// [1, 64] (identity-order regions); in the reordered article part deltas jump and it stays closed.
//
// Everything is computed from already-coded bytes (the same binary compresses and decompresses). The mixing is
// E12Mix's: outside slots p passes through; inside, a logistic mixer over stretch(p), a bias,
// stretch(StateMap[class, q(p1), byte position, bit]) and stretch(p1), weight set (class, state, bit).
// Offline twin: scripts/analysis/x2_meta2.py (walk) + x2_sim.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace x2 {

enum Cls { TIME, RPID, NCLS };

struct Cand { std::string s; double w; };

class TimeLaw {
 public:
  bool ok = false;
  long slots_opened[NCLS] = {0};

  TimeLaw(const char* dict_path, bool coded_block_header) : skip_(coded_block_header ? 6 : 1) {
    for (int c = 0; c < 256; c++) {
      int d = c;
      if ('{' <= d && d < 127) d += 'P' - '{';
      else if ('P' <= d && d < 'T') d -= 'P' - '{';
      else if ((':' <= d && d <= '?') || ('J' <= d && d <= 'O')) d ^= 0x70;
      if (d == 'X' || d == '`') d ^= 'X' ^ '`';
      SWAP_[c] = d;
    }
    for (int c = 0; c < 256; c++) ENC_[SWAP_[c] & 0xFF] = (uint8_t)c;
    FILE* f = fopen(dict_path, "rb");
    if (!f) return;
    std::string data;
    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, r);
    fclose(f);
    std::vector<std::string> words;
    std::string cur;
    for (char c : data) {
      if (c >= 'a' && c <= 'z') cur += c;
      else if (!cur.empty()) { words.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) words.push_back(cur);
    const int b1 = 80, b2 = 80 + 3840, b3 = 80 + 3840 + 40960;
    for (int n = 0; n < (int)words.size() && n < b3; n++) {
      uint32_t code;
      if (n < b1) code = 0x80 + n;
      else if (n < b2) code = (0xD0 + (n - b1) / 80) + ((0x80 + (n - b1) % 80) << 8);
      else { int m = n - b2; code = (0xF0 + (m / 80) / 32) + ((0xD0 + (m / 80) % 32) << 8) + ((0x80 + m % 80) << 16); }
      dict_[code] = words[n];
    }
    for (int i = 0; i < 32; i++) { sc_sum_[i] = 8.0; sc_n_[i] = 1.0; }
    ok = true;
  }

  bool InSlot(int* cls) const { if (active_) *cls = cls_; return active_; }
  // Analysis only (scripts/analysis/x2_csim.cpp): pre-load an earlier record of the full block, so a proxy can be
  // simulated with the knowledge a full run would have. cmix never calls it.
  void Seed(int64_t rev, int64_t T, int64_t pid) {
    if (rev >= HI) cal_[rev] = T;
    if (pid >= 0 && pid < 50000000) {
      if ((size_t)pid >= used_.size()) used_.resize((size_t)pid + 65536, false);
      used_[pid] = true;
    }
  }
  bool Predict(double* p1, int* st, int* bytepos) {
    if (!active_ || !alive_) return false;
    double m0 = 0, m1 = 0;
    const int b = bit_;
    for (size_t t = lo_; t < hi_; t++) {
      const std::string& cs = cands_[t].s;
      if (cs.size() <= pos_) continue;
      int cb = (uint8_t)cs[pos_];
      if (b > 0 && (cb >> (8 - b)) != partial_) continue;
      if ((cb >> (7 - b)) & 1) m1 += cands_[t].w; else m0 += cands_[t].w;
    }
    if (m0 + m1 <= 0) { alive_ = false; return false; }
    *p1 = m1 / (m0 + m1);
    *st = m0 == 0 ? 1 : m1 == 0 ? 0 : 2;
    *bytepos = (int)pos_;
    return true;
  }
  void Bit(int bit) {
    partial_ = (partial_ << 1) | bit;
    if (++bit_ == 8) { Byte((uint8_t)partial_); partial_ = 0; bit_ = 0; }
  }

 private:
  static constexpr int64_t HI = 18100000;
  int SWAP_[256];
  uint8_t ENC_[256];
  std::unordered_map<uint32_t, std::string> dict_;
  long nbytes_ = 0;
  int skip_;
  bool upper_ = false, capital_ = false, esc_ = false;
  int code_need_ = 0, code_shift_ = 0;
  uint32_t code_ = 0;

  // slot following (as e12::ArithSpec)
  bool active_ = false, alive_ = false;
  int cls_ = 0, bit_ = 0, partial_ = 0;
  size_t pos_ = 0, lo_ = 0, hi_ = 0, maxlen_ = 0;
  std::vector<Cand> cands_;

  // metadata-block state
  std::string line_, prev_line_;
  int64_t rev_ = -1;         // revision id of the current record (-1: none yet)
  bool want_rev_ = false;    // the next "id>" line is the revision id
  int64_t prev_pid_ = 0;
  bool have_pid_ = false;
  int good_run_ = 0;         // consecutive records with a page-id delta in [1, 64]
  std::vector<bool> used_;   // page ids seen
  std::map<int64_t, int64_t> cal_;  // revision id (>= HI) -> time
  double sc_sum_[32], sc_n_[32];
  bool pending_ = false;     // a ">DELTA" line was just seen; it's a record if "revision>" follows
  int64_t pending_delta_ = 0;
  // the open time slot's prediction (for the scale update)
  bool ts_pred_ = false; double ts_p_ = 0; int ts_gb_ = 0;

  std::string enc(const std::string& s) const {
    std::string o(s.size(), 0);
    for (size_t i = 0; i < s.size(); i++) o[i] = (char)ENC_[(uint8_t)s[i]];
    return o;
  }
  static std::string ts_str(int64_t T) {
    int64_t days = T / 86400, s = T % 86400, y = days / 372, d = days % 372;
    char buf[64];
    snprintf(buf, sizeof buf, "0%lld%lld:%lld\n", (long long)y, (long long)d, (long long)s);
    return buf;
  }
  void open_slot(int cls, std::vector<Cand>&& c) {
    if (c.empty()) return;
    std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.s < b.s; });
    // merge duplicates (not expected)
    cands_.clear();
    for (auto& x : c) {
      if (!cands_.empty() && cands_.back().s == x.s) cands_.back().w += x.w; else cands_.push_back(x);
    }
    maxlen_ = 0;
    for (auto& x : cands_) maxlen_ = std::max(maxlen_, x.s.size());
    active_ = alive_ = true;
    cls_ = cls; pos_ = 0; lo_ = 0; hi_ = cands_.size();
    slots_opened[cls]++;
  }
  void follow(uint8_t byte) {
    if (!active_) return;
    if (alive_) {
      size_t nlo = hi_, nhi = lo_;
      for (size_t t = lo_; t < hi_; t++) {
        const std::string& cs = cands_[t].s;
        if (cs.size() > pos_ && (uint8_t)cs[pos_] == byte) { nlo = std::min(nlo, t); nhi = std::max(nhi, t + 1); }
      }
      if (nlo >= nhi) alive_ = false; else { lo_ = nlo; hi_ = nhi; }
    }
    pos_++;
    bool close = pos_ >= maxlen_;
    if (alive_) {
      bool all_end = true;
      for (size_t t = lo_; t < hi_; t++) if (cands_[t].s.size() > pos_) { all_end = false; break; }
      if (all_end) close = true;
    }
    if (SWAP_[byte] == '\n') close = true;  // every candidate ends with the newline
    if (close) active_ = false;
  }

  void Byte(uint8_t byte) {
    follow(byte);
    nbytes_++;
    if (nbytes_ <= skip_) return;
    int c = SWAP_[byte];
    if (code_need_ > 0) {
      code_ += (uint32_t)c << code_shift_;
      code_shift_ += 8;
      if (c > 0xCF && code_shift_ < 24) return;
      code_need_ = 0;
      emit_word();
    } else if (esc_) {
      esc_ = false; upper_ = false; text((char)c);
    } else if (c == 0x0C) { esc_ = true; }
    else if (c == 0x07) { upper_ = true; }
    else if (c == 0x40) { capital_ = true; }
    else if (c == 0x06) { upper_ = false; }
    else if (c >= 0x80) {
      code_ = c; code_shift_ = 8;
      if (c > 0xCF) { code_need_ = 1; return; }
      emit_word();
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) upper_ = false;
      if (capital_ || upper_) c = c - 'a' + 'A';
      capital_ = false;
      text((char)(c & 0xFF));
    }
  }
  void emit_word() {
    auto it = dict_.find(code_);
    std::string w = it == dict_.end() ? std::string() : it->second;
    if (!w.empty()) {
      if (capital_) { w[0] -= 32; capital_ = false; }
      if (upper_) for (auto& ch : w) ch = (char)toupper((unsigned char)ch);
    }
    for (char ch : w) text(ch);
  }

  static bool all_digits(const std::string& s, size_t from) {
    if (s.size() <= from) return false;
    for (size_t i = from; i < s.size(); i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
  }

  void text(char ch) {
    if (ch != '\n') {
      if (line_.size() < 256) line_ += ch;
      if (!active_) {
        if (line_ == "timestamp>") try_time();
        else if (line_ == ">" && prev_line_ == "/contributor>") try_rpid();
      }
      return;
    }
    end_line();
    prev_line_.swap(line_);
    line_.clear();
  }

  void end_line() {
    const std::string& L = line_;
    bool was_pending = pending_;
    pending_ = false;
    if (L.size() >= 2 && L.size() < 16 && L[0] == '>' && (all_digits(L, 1) || (L[1] == '-' && all_digits(L, 2)))
        && (prev_line_ == "/contributor>" || !have_pid_)) {
      pending_ = true;  // a record's page-id delta, if "revision>" follows
      pending_delta_ = atoll(L.c_str() + 1);
    } else if (L == "revision>") {
      if (was_pending) {
        int64_t delta = pending_delta_;
        int64_t pid = (have_pid_ ? prev_pid_ : 0) + delta;
        good_run_ = (have_pid_ && delta >= 1 && delta <= 64) ? good_run_ + 1 : 0;
        prev_pid_ = pid; have_pid_ = true;
        if (pid >= 0 && pid < 50000000) {
          if ((size_t)pid >= used_.size()) used_.resize((size_t)pid + 65536, false);
          used_[pid] = true;
        }
      }
      want_rev_ = true; rev_ = -1;
    } else if (want_rev_ && L.size() > 3 && L.compare(0, 3, "id>") == 0 && all_digits(L, 3)) {
      rev_ = atoll(L.c_str() + 3); want_rev_ = false;
    } else if (L.size() > 11 && L.compare(0, 11, "timestamp>0") == 0) {
      size_t colon = L.find(':');
      if (rev_ >= HI && colon != std::string::npos && colon > 12 && all_digits(L, colon + 1)) {
        bool okd = true;
        for (size_t i = 11; i < colon; i++) if (L[i] < '0' || L[i] > '9') okd = false;
        if (okd) {
          int64_t y = L[11] - '0';
          int64_t d = atoll(L.substr(12, colon - 12).c_str());
          int64_t s = atoll(L.c_str() + colon + 1);
          int64_t T = (y * 372 + d) * 86400 + s;
          if (ts_pred_) {
            double e = std::fabs((double)T - ts_p_);
            sc_sum_[ts_gb_] += e < 30000 ? e : 30000;
            sc_n_[ts_gb_] += 1;
          }
          cal_[rev_] = T;
        }
      }
      ts_pred_ = false;
    }
  }

  void try_time() {
    ts_pred_ = false;
    if (rev_ < HI || cal_.empty()) return;
    auto it = cal_.lower_bound(rev_);
    if (it == cal_.end() || it == cal_.begin() || it->first == rev_) return;
    auto lo = std::prev(it);
    int64_t r0 = lo->first, r1 = it->first, t0 = lo->second, t1 = it->second;
    double p = (double)t0 + (double)(t1 - t0) * (double)(rev_ - r0) / (double)(r1 - r0);
    int64_t gap = r1 - r0;
    int gb = std::min(30, (int)std::floor(std::log2((double)gap + 1)));
    double b = std::max(0.7, sc_sum_[gb] / sc_n_[gb]);
    double W = std::min(2000.0, std::max(10.0, 8.0 * b));
    int64_t a = std::max<int64_t>(t0, (int64_t)std::floor(p) - (int64_t)W);
    int64_t z = std::min<int64_t>(t1, (int64_t)std::ceil(p) + (int64_t)W);
    std::vector<Cand> c;
    c.reserve((size_t)std::max<int64_t>(0, z - a + 1));
    for (int64_t T = a; T <= z; T++) c.push_back({enc(ts_str(T)), std::exp(-std::fabs((double)T - p) / b)});
    ts_pred_ = true; ts_p_ = p; ts_gb_ = gb;
    open_slot(TIME, std::move(c));
  }

  void try_rpid() {
    if (!have_pid_ || good_run_ < 4) return;
    std::vector<Cand> c;
    int64_t x = prev_pid_ + 1;
    char buf[32];
    while ((int)c.size() < 64) {
      if (x >= (int64_t)used_.size() || !used_[x]) {
        snprintf(buf, sizeof buf, "%lld\n", (long long)(x - prev_pid_));
        c.push_back({enc(buf), std::pow(1.0 + c.size(), -1.5)});
      }
      x++;
    }
    open_slot(RPID, std::move(c));
  }
};

class TimeLawMix {
 public:
  TimeLawMix(const char* dict, bool coded_block_header, double lr = 0.002)
      : spec(dict, coded_block_header), lr_(lr), smp_(NCLS * 14 * 4 * 8, 0.5f), smn_(NCLS * 14 * 4 * 8, 0),
        w_(NCLS * 4 * 8 * 4, 0.0) {
    for (size_t s = 0; s < w_.size(); s += 4) w_[s] = 1.0;
  }
  TimeLaw spec;
  float Predict(float p_in) {
    on_ = false;
    int cls;
    if (!spec.InSlot(&cls)) return p_in;
    double pr = clampp((double)p_in);
    x_[0] = stretch(pr); x_[1] = 0.3; x_[2] = 0; x_[3] = 0;
    smi_ = -1;
    double p1; int st = 3, bp = 0;
    if (spec.Predict(&p1, &st, &bp)) {
      int pbk = bp == 0 ? 0 : bp == 1 ? 1 : bp < 4 ? 2 : 3;
      smi_ = ((cls * 14 + qp(p1)) * 4 + pbk) * 8 + bit_;
      x_[2] = stretch(clampp(smp_[smi_], 1e-6));
      x_[3] = stretch(clampp(p1, 1e-4));
    }
    set_ = ((cls * 4 + st) * 8 + bit_) * 4;
    double d = 0;
    for (int i = 0; i < 4; i++) d += w_[set_ + i] * x_[i];
    pf_ = clampp(squash(d));
    on_ = true;
    return (float)pf_;
  }
  void Update(int y) {
    if (on_) {
      double err = y - pf_;
      for (int i = 0; i < 4; i++) w_[set_ + i] += lr_ * err * x_[i];
      if (smi_ >= 0) {
        if (smn_[smi_] < 1023) smn_[smi_]++;
        smp_[smi_] += (y - smp_[smi_]) / (smn_[smi_] + 0.5);
      }
    }
    spec.Bit(y);
    bit_ = (bit_ + 1) & 7;
  }

 private:
  static double stretch(double p) { return std::log(p / (1 - p)); }
  static double squash(double x) { if (x > 40) x = 40; if (x < -40) x = -40; return 1 / (1 + std::exp(-x)); }
  static double clampp(double p, double e = 1e-6) { return p < e ? e : (p > 1 - e ? 1 - e : p); }
  static int qp(double p1) {
    if (p1 <= 0) return 0;
    if (p1 >= 1) return 13;
    double st = stretch(clampp(p1, 1e-5));
    int q = (int)std::floor((st + 9) / 1.5) + 1;
    return q < 1 ? 1 : q > 12 ? 12 : q;
  }
  double lr_;
  std::vector<float> smp_;
  std::vector<uint16_t> smn_;
  std::vector<double> w_;
  double x_[4], pf_ = 0.5;
  int set_ = 0, smi_ = -1, bit_ = 0;
  bool on_ = false;
};

}  // namespace x2
