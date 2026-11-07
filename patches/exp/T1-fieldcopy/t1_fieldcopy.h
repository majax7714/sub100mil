// T1: interlanguage title = English title ("field copy" across phda9's block split), as a final stage after X2.
//
// phda9 moves an article's trailing interlanguage links, and its "</text>", into one group of a block that follows
// the metadata block (after the last page):
//   "...\n/contributor>\n[ar:...]\n[de:Albedo]\n...[sv:Albedo]</text>\n[de:Autismus]\n...</text>\n..."
// The article keeps no "</text>", so the k-th page without "</text>" (and without an empty <text ... />) owns the
// k-th group, and every title has been decoded before the block starts. That holds in any page order (the full
// run's reordered order included), so the law is decoder-valid.
//
// Law: a link's title is often the English title itself (de/da/fr/io/af/... for names, places, years). At every
// link title (after "[xx:") a slot opens with one candidate: the coded bytes of the owning page's <title> as the
// title line coded them, plus "]" (and the title without a trailing " (...)" at weight 0.3). The slot follows the
// coded bytes and closes at the first byte that leaves every candidate, or at the candidate's end.
// Slot class (from earlier links only): group history (first link / an earlier link of the group was the title /
// none was) x the language's running rate of title links (Laplace, buckets <0.05, <0.2, <0.5, >=0.5).
//
// Mixing is E12Mix's/X2's: outside slots p passes through; inside, a logistic mixer over stretch(p), a bias,
// stretch(StateMap[class, q(p1), byte position, bit]) and stretch(p1), weight set (class, state, bit).
// Offline twin: scripts/analysis/t1_slots.py + t1_sim.cpp; causal re-run of this header: t1_csim.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace t1 {

static const int NCLS = 12;

struct Cand { std::string s; double w; };

class FieldCopy {
 public:
  bool ok = false;
  long slots_opened = 0, groups_seen = 0, titles_queued = 0, eq_links = 0;

  FieldCopy(const char* dict_path, bool coded_block_header) : skip_(coded_block_header ? 6 : 1) {
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
    ok = true;
  }

  bool InSlot(int* cls) const { if (active_) *cls = cls_; return active_; }
  // Analysis only (scripts/analysis/t1_csim.cpp): pre-load a language's title-link counts from the full run's
  // earlier groups, so a proxy can be simulated with the rates a full run would have. cmix never calls it.
  void Seed(const std::string& lang, int eq, int n) { rate_[lang].first += eq; rate_[lang].second += n; }
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
    if (m0 + m1 <= 0) return false;
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
  int SWAP_[256];
  uint8_t ENC_[256];
  std::unordered_map<uint32_t, std::string> dict_;
  long nbytes_ = 0;
  int skip_;
  bool upper_ = false, capital_ = false, esc_ = false;
  int code_need_ = 0, code_shift_ = 0;
  uint32_t code_ = 0;

  // slot following
  bool active_ = false, alive_ = false, t_alive_ = false;
  int cls_ = 0, bit_ = 0, partial_ = 0;
  size_t pos_ = 0, lo_ = 0, hi_ = 0, maxlen_ = 0, tlen_ = 0;
  std::vector<Cand> cands_;
  std::string tcand_;  // the title candidate (to tell an exact title link from the others)

  // line state: decoded text of the current/previous line, and, for a line that may be a <title> line, every
  // coded byte with the line offsets before and after it
  std::string line_, prev_line_;
  size_t line_len_ = 0;
  bool title_line_ = true;
  std::vector<uint8_t> lb_;
  std::vector<uint16_t> lb_s_, lb_e_;

  // pages and groups
  bool in_page_ = false, closed_ = false, empty_text_ = false;
  std::string tail7_;
  std::string page_title_, page_np_;  // coded title, coded title without " (...)" (empty if none)
  std::vector<std::string> titles_, nps_;
  bool iw_ = false;
  size_t group_ = 0;
  int grp_ = 0;  // 0 first link of the group, 1 an earlier link was the title, 2 none was
  bool link_open_ = false, link_eq_ = false;
  std::string link_lang_;
  std::unordered_map<std::string, std::pair<int, int>> rate_;

  void open_slot(int cls, std::vector<Cand>&& c) {
    std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.s < b.s; });
    cands_.clear();
    for (auto& x : c) {
      if (x.s.empty()) continue;
      if (!cands_.empty() && cands_.back().s == x.s) cands_.back().w += x.w; else cands_.push_back(x);
    }
    if (cands_.empty()) return;
    maxlen_ = 0;
    for (auto& x : cands_) maxlen_ = std::max(maxlen_, x.s.size());
    active_ = alive_ = t_alive_ = true;
    cls_ = cls; pos_ = 0; lo_ = 0; hi_ = cands_.size();
    slots_opened++;
  }
  void follow(uint8_t byte) {
    if (!active_) return;
    if (t_alive_ && !(pos_ < tcand_.size() && (uint8_t)tcand_[pos_] == byte)) t_alive_ = false;
    size_t nlo = hi_, nhi = lo_;
    for (size_t t = lo_; t < hi_; t++) {
      const std::string& cs = cands_[t].s;
      if (cs.size() > pos_ && (uint8_t)cs[pos_] == byte) { nlo = std::min(nlo, t); nhi = std::max(nhi, t + 1); }
    }
    pos_++;
    if (t_alive_ && pos_ == tcand_.size()) link_eq_ = true;
    if (nlo >= nhi) { active_ = alive_ = false; return; }  // left every candidate: close (this byte was the last)
    lo_ = nlo; hi_ = nhi;
    bool all_end = true;
    for (size_t t = lo_; t < hi_; t++) if (cands_[t].s.size() > pos_) { all_end = false; break; }
    if (all_end || pos_ >= maxlen_) active_ = alive_ = false;
  }

  void Byte(uint8_t byte) {
    follow(byte);
    nbytes_++;
    if (nbytes_ <= skip_) return;
    size_t before = line_len_;
    bool nl = decode(byte);
    if (title_line_ && !nl && lb_.size() < 2048) {
      lb_.push_back(byte); lb_s_.push_back((uint16_t)std::min<size_t>(before, 65535));
      lb_e_.push_back((uint16_t)std::min<size_t>(line_len_, 65535));
    }
  }
  // returns true if this byte ended a line
  bool decode(uint8_t byte) {
    int c = SWAP_[byte];
    if (code_need_ > 0) {
      code_ += (uint32_t)c << code_shift_;
      code_shift_ += 8;
      if (c > 0xCF && code_shift_ < 24) return false;
      code_need_ = 0;
      emit_word();
    } else if (esc_) {
      esc_ = false; upper_ = false; return text((char)c);
    } else if (c == 0x0C) { esc_ = true; }
    else if (c == 0x07) { upper_ = true; }
    else if (c == 0x40) { capital_ = true; }
    else if (c == 0x06) { upper_ = false; }
    else if (c >= 0x80) {
      code_ = c; code_shift_ = 8;
      if (c > 0xCF) { code_need_ = 1; return false; }
      emit_word();
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) upper_ = false;
      if (capital_ || upper_) c = c - 'a' + 'A';
      capital_ = false;
      return text((char)(c & 0xFF));
    }
    return false;
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

  bool text(char ch) {
    if (ch != '\n') {
      if (line_.size() < 512) line_ += ch;
      line_len_++;
      if (title_line_ && line_len_ <= 11 && line_.compare(0, line_len_, "    <title>", line_len_) != 0) title_line_ = false;
      if (in_page_) {
        tail7_ += ch;
        if (tail7_.size() > 7) tail7_.erase(0, tail7_.size() - 7);
        if (tail7_ == "</text>") closed_ = true;
      }
      if (ch == ':' && !active_ && line_.size() <= 14 && line_[0] == '[' && (iw_ || prev_line_ == "/contributor>"))
        try_link();
      return false;
    }
    end_line();
    prev_line_.swap(line_);
    line_.clear(); line_len_ = 0; title_line_ = true;
    lb_.clear(); lb_s_.clear(); lb_e_.clear();
    return true;
  }

  void end_line() {
    const std::string& L = line_;
    if (link_open_) {
      // the link's line is over: update the language's rate and the group history
      auto& r = rate_[link_lang_];
      r.first += link_eq_; r.second += 1;
      eq_links += link_eq_;
      grp_ = (grp_ == 1 || link_eq_) ? 1 : 2;
      link_open_ = false;
    }
    if (iw_ && L.size() >= 7 && L.compare(L.size() - 7, 7, "</text>") == 0) {  // a group ends
      group_++; grp_ = 0; groups_seen++;
      return;
    }
    if (L == "  <page>") {
      in_page_ = true; closed_ = false; empty_text_ = false; tail7_.clear(); page_title_.clear(); page_np_.clear();
    } else if (in_page_ && L.size() > 19 && L.compare(0, 11, "    <title>") == 0 &&
               L.compare(L.size() - 8, 8, "</title>") == 0 && line_len_ == L.size()) {
      size_t tl = L.size() - 19, npl = tl;
      size_t lp = L.rfind(" (", L.size() - 8);
      if (lp != std::string::npos && lp > 11 && L[L.size() - 9] == ')') npl = lp - 11;
      for (size_t i = 0; i < lb_.size(); i++) {
        if (lb_s_[i] >= 11 && lb_s_[i] < 11 + tl && lb_e_[i] <= 11 + tl) page_title_ += (char)lb_[i];
        if (npl < tl && lb_s_[i] >= 11 && lb_s_[i] < 11 + npl && lb_e_[i] <= 11 + npl) page_np_ += (char)lb_[i];
      }
    } else if (in_page_ && L == "      <text xml:space=\"preserve\" />") {
      empty_text_ = true;
    } else if (in_page_ && L == "  </page>") {
      if (!closed_ && !empty_text_ && !page_title_.empty()) {
        titles_.push_back(page_title_); nps_.push_back(page_np_); titles_queued++;
      }
      in_page_ = false;
    }
  }

  void try_link() {
    const std::string& L = line_;  // "[" lang ":"
    size_t n = L.size() - 2;
    if (n < 2 || n > 12 || L[1] < 'a' || L[1] > 'z') return;
    for (size_t i = 1; i <= n; i++) if (!((L[i] >= 'a' && L[i] <= 'z') || L[i] == '-')) return;
    if (!iw_) { iw_ = true; group_ = 0; grp_ = 0; }
    if (group_ >= titles_.size()) return;
    link_open_ = true; link_eq_ = false;
    link_lang_ = L.substr(1, n);
    auto it = rate_.find(link_lang_);
    double rr = it == rate_.end() ? 0.05 : (it->second.first + 0.05) / (it->second.second + 1.0);
    int lb = rr < 0.05 ? 0 : rr < 0.2 ? 1 : rr < 0.5 ? 2 : 3;
    std::string close(1, (char)ENC_[(uint8_t)']']);
    tcand_ = titles_[group_] + close;
    std::vector<Cand> c;
    c.push_back({tcand_, 1.0});
    if (!nps_[group_].empty()) c.push_back({nps_[group_] + close, 0.3});
    open_slot(grp_ * 4 + lb, std::move(c));
  }
};

class FieldCopyMix {
 public:
  FieldCopyMix(const char* dict, bool coded_block_header, double lr = 0.004)
      : spec(dict, coded_block_header), lr_(lr), smp_(NCLS * 14 * 4 * 8, 0.5f), smn_(NCLS * 14 * 4 * 8, 0),
        w_(NCLS * 4 * 8 * 4, 0.0) {
    for (size_t s = 0; s < w_.size(); s += 4) w_[s] = 1.0;
  }
  FieldCopy spec;
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

}  // namespace t1
