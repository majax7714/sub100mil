// E12: arithmetic-consistency specialist ("numbers that are functions of other numbers").
//
// A causal model over the coded (WRT) stream. It WRT-decodes the bytes already coded (with the same
// english.dic the preprocessor uses), keeps the text of the current article, and when the text so far ends in
// a known trigger it opens a *slot*: a weighted set of candidate strings (a number plus the byte that ends it)
// derived from numbers earlier in the same article. While a slot is open it predicts each bit from the candidate
// masses consistent with the bits coded so far, and abstains once no candidate is consistent.
//
// Slot classes (US census "Rambot" articles, plus general unit conversions):
//   race%, race%last, hisp%   k/P lattice (2 decimals), the last race item = 100 - sum of the others
//   hh_copy                   "There are H households" repeats H
//   hh%, hh%cons              k/H lattice (1 decimal), non-families <= 100-married-female, alone65 <= individuals
//   hhsize, famsize           household population / H (= P/H without group quarters), family population / F
//   age%, age%last            k/P lattice (1 decimal), the last item = 100 - sum
//   sexratio, sexratio18      100 M/(D-M) for integer M (D = P, or adults = P - under-18 count)
//   area_mi .. water%         km^2 -> mi^2 (factor 2.589988), land = total, water = total - land, water% = W/T
//   dens_km .. hdens_mi       population and housing density from land area and counts
//   coord                     decimal degrees from the preceding D deg M' S"
//   conv                      "N unit (" -> converted value, several roundings
// The candidate rules mirror scripts/analysis/e12_slots.py (the offline simulation), but every trigger here is a
// left context only (the simulation's regexes also saw the slot's right context).
//
// Determinism: everything is computed from already-coded bytes with the same double arithmetic and snprintf in
// the compressor and the decompressor (same binary).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace e12 {

enum Cls {
  RACE, RACE_LAST, HISP, HH_COPY, HH, HH_CONS, HHSIZE, FAMSIZE, AGE, AGE_LAST, SEXRATIO, SEXRATIO18, AREA_MI,
  LAND_KM, LAND_MI, WATER_KM, WATER_MI, WATER_PCT, DENS_KM, DENS_MI, HDENS_KM, HDENS_MI, COORD, CONV, NCLS
};
static const char* const kClassNames[NCLS] = {
    "race%", "race%last", "hisp%", "hh_copy", "hh%", "hh%cons", "hhsize", "famsize", "age%", "age%last",
    "sexratio", "sexratio18", "area_mi", "land_km", "land_mi", "water_km", "water_mi", "water%",
    "dens_km", "dens_mi", "hdens_km", "hdens_mi", "coord", "conv"};

static const double KM2 = 2.589988110336;

struct Cand { std::string s; double w; };

// ------------------------------------------------------------------------------------------------ helpers
inline double fnum(const std::string& s) {
  std::string t;
  for (char c : s) if (c != ',') t += c;
  return atof(t.c_str());
}
inline double half_ulp(const std::string& s) {
  size_t d = s.find('.');
  int dec = d == std::string::npos ? 0 : (int)(s.size() - d - 1);
  return 0.5 * std::pow(10.0, -dec);
}
inline std::string fmtd(double v, int dec, bool commas) {
  char buf[64];
  snprintf(buf, sizeof buf, "%.*f", dec, v);
  std::string s(buf);
  if (!commas) return s;
  bool neg = s[0] == '-';
  if (neg) s = s.substr(1);
  size_t dot = s.find('.');
  std::string ip = s.substr(0, dot), fp = dot == std::string::npos ? "" : s.substr(dot);
  std::string o;
  int n = ip.size();
  for (int i = 0; i < n; i++) { o += ip[i]; if ((n - 1 - i) % 3 == 0 && i != n - 1) o += ','; }
  return (neg ? "-" : "") + o + fp;
}
inline std::string mi_str(double v) {  // Rambot: 1 decimal, 2 when that would print 0.0
  char buf[64];
  double r = std::nearbyint(v * 10) / 10;  // only used for the < 0.1 test
  snprintf(buf, sizeof buf, (r < 0.1 && v > 0) ? "%.2f" : "%.1f", v);
  return buf;
}

typedef std::map<std::string, double> CandMap;

inline void add_norm(CandMap& dst, const CandMap& src, double w) {
  double t = 0;
  for (auto& kv : src) t += kv.second;
  if (t <= 0) return;
  for (auto& kv : src) dst[kv.first] += w * kv.second / t;
}
// values scale*k/D (kind 1) or scale*k/(D-k) (kind 2), k in [kmin, kmax], kept if in [lo, hi]
inline CandMap lattice(int kind, int64_t D, int64_t kmin, int64_t kmax, double scale, int dec, char term,
                       double lo = -1e18, double hi = 1e18) {
  CandMap m;
  char buf[64];
  if (kmin < 0) kmin = 0;
  for (int64_t k = kmin; k <= kmax; k++) {
    double v;
    if (kind == 1) v = scale * (double)k / (double)D;
    else { if (k >= D) break; v = scale * (double)k / (double)(D - k); }
    if (v < lo - 1e-9 || v > hi + 1e-9) continue;
    int l = snprintf(buf, sizeof buf, "%.*f", dec, v);
    buf[l] = term;
    m[std::string(buf, l + 1)] += 1;
  }
  return m;
}
inline bool range_list(CandMap& m, double lo, double hi, int dec, const std::string& term, bool commas,
                       int maxn = 4000, const char* sign = nullptr) {
  double u = std::pow(10.0, -dec);
  long long a = (long long)std::floor(lo / u + 0.5), b = (long long)std::floor(hi / u + 0.5);
  if (b < a || b - a + 1 > maxn) return false;
  for (long long k = a; k <= b; k++) {
    double v = k * u;
    std::string s = sign ? std::string(sign) + fmtd(v, dec, false) : fmtd(v, dec, commas);
    m[s + term] += 1;
  }
  return true;
}
inline std::pair<double, double> iand(std::pair<double, double> a, std::pair<double, double> b) {
  double lo = std::max(a.first, b.first), hi = std::min(a.second, b.second);
  if (lo > hi) return a;
  return {lo, hi};
}

static const char* NUM = "([0-9][0-9,]*(?:\\.[0-9]+)?)";

// ------------------------------------------------------------------------------------------------ the model
class ArithSpec {
 public:
  bool ok = false;

  explicit ArithSpec(const char* dict_path) {
    init_swap();
    FILE* f = fopen(dict_path, "rb");
    if (!f) return;
    std::string data;
    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, r);
    fclose(f);
    // words = all [a-z]+ runs of the dictionary file (as a1_analyze.load_dict)
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
    compile();
    ok = true;
  }

  // ---- per-bit interface
  // Returns true when the specialist has an opinion on the next bit: p1 = P(bit = 1), cls, state (0: only 0
  // possible, 1: only 1 possible, 2: both), bytepos = position of the current byte in the slot.
  bool Predict(double* p1, int* cls, int* st, int* bytepos) {
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
    *cls = cls_;
    *bytepos = (int)pos_;
    return true;
  }
  bool InSlot(int* cls) const { if (active_) *cls = cls_; return active_; }

  void Bit(int bit) {
    partial_ = (partial_ << 1) | bit;
    if (++bit_ == 8) {
      Byte((uint8_t)partial_);
      partial_ = 0;
      bit_ = 0;
    }
  }

  // stats
  long slots_opened[NCLS] = {0};

 private:
  // ---------------------------------------------------------------- slot following
  bool active_ = false, alive_ = false;
  int cls_ = 0;
  size_t pos_ = 0, lo_ = 0, hi_ = 0, maxlen_ = 0;
  std::vector<Cand> cands_;
  int bit_ = 0, partial_ = 0;

  void open_slot(int cls, const CandMap& m) {
    if (m.empty()) return;
    cands_.clear();
    maxlen_ = 0;
    for (auto& kv : m) { cands_.push_back({kv.first, kv.second}); maxlen_ = std::max(maxlen_, kv.first.size()); }
    active_ = alive_ = true;
    cls_ = cls;
    pos_ = 0; lo_ = 0; hi_ = cands_.size();
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
    // the slot closes after its terminator (all candidates of the consistent range have ended), or once dead
    // and the number has ended (a non-number byte), or at the longest candidate's length.
    bool close = pos_ >= maxlen_;
    if (alive_) {
      bool all_end = true;
      for (size_t t = lo_; t < hi_; t++) if (cands_[t].s.size() > pos_) { all_end = false; break; }
      if (all_end) close = true;
    } else {
      int c = SWAP_[byte];
      if (!((c >= '0' && c <= '9') || c == '.' || c == ',')) close = true;
    }
    if (close) active_ = false;
  }

  // ---------------------------------------------------------------- WRT decoding
  int SWAP_[256];
  std::unordered_map<uint32_t, std::string> dict_;
  long nbytes_ = 0;
  bool upper_ = false, capital_ = false, esc_ = false;
  int code_need_ = 0;  // >0: inside a multi-byte dictionary code
  uint32_t code_ = 0;
  int code_shift_ = 0;
  std::string art_;  // text of the current article (since the last "<page>")

  void init_swap() {
    for (int c = 0; c < 256; c++) {
      int d = c;
      if ('{' <= d && d < 127) d += 'P' - '{';
      else if ('P' <= d && d < 'T') d -= 'P' - '{';
      else if ((':' <= d && d <= '?') || ('J' <= d && d <= 'O')) d ^= 0x70;
      if (d == 'X' || d == '`') d ^= 'X' ^ '`';
      SWAP_[c] = d;
    }
  }

  void Byte(uint8_t byte) {
    follow(byte);
    nbytes_++;
    if (nbytes_ == 1) return;  // the WRT flag byte of the TEXT block
    int c = SWAP_[byte];
    size_t before = art_.size();
    if (code_need_ > 0) {
      code_ += (uint32_t)c << code_shift_;
      code_shift_ += 8;
      if (c > 0xCF && code_shift_ < 24) return;  // another code byte follows
      code_need_ = 0;
      emit_word();
    } else if (esc_) {
      esc_ = false;
      upper_ = false;
      art_ += (char)c;
    } else if (c == 0x0C) {
      esc_ = true;
      return;
    } else if (c == 0x07) { upper_ = true; return; }
    else if (c == 0x40) { capital_ = true; return; }
    else if (c == 0x06) { upper_ = false; return; }
    else if (c >= 0x80) {
      code_ = c; code_shift_ = 8;
      if (c > 0xCF) { code_need_ = 1; return; }
      emit_word();
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) upper_ = false;
      if (capital_ || upper_) c = c - 'a' + 'A';
      capital_ = false;
      art_ += (char)(c & 0xFF);
    }
    if (art_.size() > before) on_text(before);
  }
  void emit_word() {
    auto it = dict_.find(code_);
    std::string w = it == dict_.end() ? std::string() : it->second;
    if (!w.empty()) {
      if (capital_) { w[0] -= 32; capital_ = false; }
      if (upper_) for (auto& ch : w) ch = (char)toupper((unsigned char)ch);
    }
    art_ += w;
  }

  // ---------------------------------------------------------------- triggers
  struct Trig { std::regex re; int id; std::string suffix; };
  std::vector<Trig> trig_;
  std::regex re_ppl_, re_area_, re_land_, re_water_, re_dens_, re_hunits_, re_u18_, re_conv_;
  std::vector<std::pair<std::string, std::vector<std::pair<double, double>>>> units_;
  bool census_ = false;

  enum TrigId { T_AREA_MI, T_LAND_KM, T_LAND_MI, T_WATER_KM, T_WATER_MI, T_WATER_PCT, T_DENS_KM, T_DENS_MI,
                T_HDENS_KM, T_HDENS_MI, T_HISP, T_HH_COPY, T_HHSIZE, T_FAMSIZE, T_SEX, T_SEX18, T_COORD1, T_COORD2 };

  void compile() {
    auto R = [](const std::string& s) { return std::regex(s, std::regex::ECMAScript | std::regex::optimize); };
    const std::string N = NUM, KMS = "km&&sup2;", MIS = "mi&&sup2;";
    const std::string DEG = "&&\xc2\xb0";
    std::vector<std::pair<std::string, int>> t = {
        {"has a total area of " + N + " \\[square kilometer\\|km&&sup2;\\] \\($", T_AREA_MI},
        {"has a total area of " + N + " \\[square kilometer\\|km&&sup2;\\] \\(" + N + " \\[square mile\\|mi&&sup2;\\]\\)\\.  $", T_LAND_KM},
        {"\\[square mile\\|mi&&sup2;\\]\\)\\.  " + N + " km&&sup2; \\($", T_LAND_MI},
        {" mi&&sup2;\\) of it is land and $", T_WATER_KM},
        {" of it is land and " + N + " km&&sup2; \\($", T_WATER_MI},
        {" mi&&sup2;\\) of it is water\\.  The total area is $", T_WATER_PCT},
        {"The \\[population density\\] is $", T_DENS_KM},
        {"The \\[population density\\] is " + N + "/km&&sup2; \\($", T_DENS_MI},
        {"housing units at an average density of $", T_HDENS_KM},
        {"housing units at an average density of " + N + "/km&&sup2; \\($", T_HDENS_MI},
        {"races\\.  $", T_HISP},
        {"\n\nThere (?:are|were) $", T_HH_COPY},
        {"The average household size is $", T_HHSIZE},
        {"The average household size is " + N + " and the average family size is $", T_FAMSIZE},
        {"For every 100 females there (?:are|were) $", T_SEX},
        {"For every 100 females age 18 and over, there (?:are|were) $", T_SEX18},
        {"is located at ([0-9]+)" + DEG + "([0-9]+)'([0-9]+)\" (North|South), ([0-9]+)" + DEG +
             "([0-9]+)'([0-9]+)\" (West|East) \\($", T_COORD1},
        {"is located at ([0-9]+)" + DEG + "([0-9]+)'([0-9]+)\" (North|South), ([0-9]+)" + DEG +
             "([0-9]+)'([0-9]+)\" (West|East) \\((-?[0-9]+\\.[0-9]+), $", T_COORD2},
    };
    static const char* suf[] = {"] (", "]).  ", "km&&sup2; (", "of it is land and ", "km&&sup2; (",
                                "The total area is ", "density] is ", "/km&&sup2; (", "average density of ",
                                "/km&&sup2; (", "races.  ", "re ", "household size is ", "family size is ", "re ",
                                "re ", "t (", ", "};
    for (auto& p : t) trig_.push_back({R(p.first), p.second, suf[p.second]});
    re_ppl_ = R("there (?:are|were) " + N + " people, " + N + " households, and " + N + " families");
    re_area_ = R("has a total area of " + N + " \\[square kilometer\\|km&&sup2;\\] \\(" + N + " \\[square mile\\|mi&&sup2;\\]\\)\\.");
    re_land_ = R("\\)\\.  " + N + " km&&sup2; \\(" + N + " mi&&sup2;\\) of it is land");
    re_water_ = R(" of it is land and " + N + " km&&sup2; \\(" + N + " mi&&sup2;\\) of it is water");
    re_dens_ = R("The \\[population density\\] is " + N + "/km&&sup2; \\(" + N + "/mi&&sup2;\\)");
    re_hunits_ = R("There (?:are|were) " + N + " housing units at an average density of " + N + "/km&&sup2; \\($");
    const double FT = 0.3048, MI = 1.609344, LB = 0.45359237, INCH = 2.54, AC = 0.40468564224;
    units_ = {
        {"km", {{1 / MI, 0}}}, {"kilometers", {{1 / MI, 0}}}, {"kilometres", {{1 / MI, 0}}},
        {"mi", {{MI, 0}}}, {"miles", {{MI, 0}}}, {"mile", {{MI, 0}}},
        {"m", {{1 / FT, 0}}}, {"metres", {{1 / FT, 0}}}, {"meters", {{1 / FT, 0}}}, {"metre", {{1 / FT, 0}}},
        {"meter", {{1 / FT, 0}}}, {"ft", {{FT, 0}}}, {"feet", {{FT, 0}}}, {"foot", {{FT, 0}}},
        {"kg", {{1 / LB, 0}}}, {"lb", {{LB, 0}}}, {"lbs", {{LB, 0}}}, {"pounds", {{LB, 0}}},
        {"cm", {{1 / INCH, 0}}}, {"mm", {{0.1 / INCH, 0}}}, {"in", {{INCH, 0}}}, {"inches", {{INCH, 0}}},
        {"km/h", {{1 / MI, 0}}}, {"mph", {{MI, 0}}},
        {"acres", {{AC, 0}, {AC / 100, 0}, {AC * 1e4, 0}}}, {"hectares", {{1 / AC, 0}}}, {"ha", {{1 / AC, 0}}},
        {"km&&sup2;", {{1 / KM2, 0}}}, {"mi&&sup2;", {{KM2, 0}}}, {"km\xc2\xb2", {{1 / KM2, 0}}}, {"sq mi", {{KM2, 0}}},
        {"\xc2\xb0" "C", {{1.8, 32}, {1.8, 0}}}, {"&&\xc2\xb0" "C", {{1.8, 32}, {1.8, 0}}},
        {"\xc2\xb0" "F", {{1 / 1.8, -32 / 1.8}, {1 / 1.8, 0}}}, {"&&\xc2\xb0" "F", {{1 / 1.8, -32 / 1.8}, {1 / 1.8, 0}}},
        {"yards", {{0.9144, 0}}}, {"yd", {{0.9144, 0}}}, {"kW", {{1.34102, 0}}}, {"hp", {{0.7457, 0}}},
        {"t", {{1.10231, 0}, {0.984207, 0}}}, {"tonnes", {{1.10231, 0}, {0.984207, 0}}},
        {"tons", {{0.907185, 0}, {1.016047, 0}}},
    };
  }

  // Called after new text was appended to art_ (from index `from`).
  void on_text(size_t from) {
    // article boundary
    if (art_.size() >= 6) {
      size_t s = art_.size() >= from + 6 ? from : art_.size() - 6;
      size_t k = art_.find("<page>", s > 5 ? s - 5 : 0);
      if (k != std::string::npos) { art_.erase(0, k); census_ = false; }
    }
    if (!census_ && art_.size() > 20 && art_.compare(art_.size() - 8, 8, "[census]") == 0) census_ = true;
    if (active_) return;  // one slot at a time
    char last = art_.back();
    if (last == '(') try_conv();
    if (active_) return;
    if (last != ' ' && last != '(' && last != '\n') return;
    bool coord2 = last == ' ' && ends(art_, ", ") && art_.size() > 40 &&
                  art_.find("t (", art_.size() - 30) != std::string::npos;
    if (census_ || last == '(' || coord2) try_triggers();
  }

  bool tail_match(const std::regex& re, std::smatch& m, size_t win = 400) {
    size_t st = art_.size() > win ? art_.size() - win : 0;
    tail_ = art_.substr(st);
    return std::regex_search(tail_, m, re);
  }
  std::string tail_;

  struct Census {
    bool ppl = false; double P = 0, H = 0, F = 0; std::string Hs;
    bool area = false; double A = 0, B = 0;
    bool land = false; double C = 0, Dm = 0; std::string Ds;
    bool water = false; double E = 0, Fw = 0; std::string Fws;
    bool dens = false; double X = 0, Y = 0;
  };
  Census parse_census() {
    Census c;
    std::smatch m;
    if (std::regex_search(art_, m, re_ppl_)) {
      c.ppl = true; c.P = fnum(m[1]); c.H = fnum(m[2]); c.F = fnum(m[3]); c.Hs = m[2];
    }
    if (std::regex_search(art_, m, re_area_)) {
      c.area = true; c.A = fnum(m[1]); c.B = fnum(m[2]);
      std::string rest = art_.substr(m.position(0) + m.length(0) - 2);
      std::smatch l;
      if (std::regex_search(rest, l, re_land_) && l.position(0) == 0) {
        c.land = true; c.C = fnum(l[1]); c.Dm = fnum(l[2]); c.Ds = l[2];
        std::string r2 = rest.substr(l.position(0) + l.length(0) - 14);
        std::smatch w;
        if (std::regex_search(r2, w, re_water_) && w.position(0) == 0) {
          c.water = true; c.E = fnum(w[1]); c.Fw = fnum(w[2]); c.Fws = w[2];
        }
      }
    }
    if (std::regex_search(art_, m, re_dens_)) { c.dens = true; c.X = fnum(m[1]); c.Y = fnum(m[2]); }
    return c;
  }
  // percentages of the current open sentence that starts with `head` and ends with `end` (not yet seen)
  size_t rfind_win(const char* head, size_t win = 1200) const {
    size_t st = art_.size() > win ? art_.size() - win : 0;
    size_t h = std::string_view(art_).substr(st).rfind(head);
    return h == std::string::npos ? h : h + st;
  }
  bool in_sentence(const char* head, const char* end, std::vector<double>* vals, std::string* lead) {
    size_t h = rfind_win(head);
    if (h == std::string::npos) return false;
    size_t s = h + strlen(head);
    if (art_.find(end, s) != std::string::npos) return false;
    std::string sent = art_.substr(s);
    static const std::regex pct("([0-9]+\\.[0-9]+)%");
    for (auto it = std::sregex_iterator(sent.begin(), sent.end(), pct); it != std::sregex_iterator(); ++it)
      vals->push_back(fnum((*it)[1]));
    *lead = sent;
    return true;
  }
  static bool ends(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
  }

  void try_triggers() {
    std::smatch m;
    for (auto& t : trig_) {
      if (!census_ && t.id != T_COORD1 && t.id != T_COORD2) continue;
      if (!ends(art_, t.suffix.c_str())) continue;
      if (!tail_match(t.re, m)) continue;
      fire(t.id, m);
      if (active_) return;
    }
    if (!census_) return;
    char last = art_.back();
    if (last != ' ') return;
    // sentence-internal percentage lists
    std::vector<double> v; std::string sent;
    // race: "The racial makeup of the X is " ... "races."
    {
      size_t h = (ends(art_, ", ") || ends(art_, "and ") || ends(art_, " is ")) ? rfind_win("The racial makeup of the ") : std::string::npos;
      if (h != std::string::npos && art_.find("races.", h) == std::string::npos) {
        size_t is = art_.find(" is ", h);
        if (is != std::string::npos && art_.find_first_of(".\n", h + 25) > is) {
          std::string body = art_.substr(is + 4);
          if (body.empty() || ends(art_, ", ") || ends(art_, "and ")) {
            Census c = parse_census();
            if (c.ppl && c.P > 0) {
              static const std::regex pct("([0-9]+\\.[0-9]+)%");
              double tot = 0; int n = 0;
              for (auto it = std::sregex_iterator(body.begin(), body.end(), pct); it != std::sregex_iterator(); ++it) { tot += fnum((*it)[1]); n++; }
              if (ends(art_, "and ")) {
                double tgt = 100.0 - tot;
                CandMap mm;
                add_norm(mm, lattice(1, c.P, 0, c.P, 100, 2, '%', tgt - 0.005 * (n + 1) - 1e-9, tgt + 0.005 * (n + 1) + 1e-9), 0.9);
                add_norm(mm, lattice(1, c.P, 0, c.P, 100, 2, '%'), 0.1);
                open_slot(RACE_LAST, mm);
              } else {
                open_slot(RACE, lattice(1, c.P, 0, c.P, 100, 2, '%', 0, 100.0 - tot + 0.005 * (n + 1)));
              }
              return;
            }
          }
        }
      }
    }
    // households: "households out of which " ... "older."
    v.clear();
    bool sep = ends(art_, ", ") || ends(art_, "and ") || ends(art_, ". ") || ends(art_, "which ") || ends(art_, "with ");
    if (!sep) return;
    if (in_sentence("households out of which ", "older.", &v, &sent)) {
      bool trig = sent.empty() || ends(art_, ", ") || ends(art_, "and ") || ends(art_, ". ");
      if (trig) {
        Census c = parse_census();
        if (c.ppl && c.H > 0) {
          size_t i = v.size();
          CandMap mm;
          if (i == 3) {
            add_norm(mm, lattice(1, c.H, 0, c.H, 100, 1, '%', 0, 100.0 - v[1] - v[2] + 0.1), 0.95);
            add_norm(mm, lattice(1, c.H, 0, c.H, 100, 1, '%'), 0.05);
            open_slot(HH_CONS, mm);
          } else if (i == 5) {
            add_norm(mm, lattice(1, c.H, 0, c.H, 100, 1, '%', 0, v[4] + 1e-9), 0.95);
            add_norm(mm, lattice(1, c.H, 0, c.H, 100, 1, '%'), 0.05);
            open_slot(HH_CONS, mm);
          } else {
            open_slot(HH, lattice(1, c.H, 0, c.H, 100, 1, '%'));
          }
          return;
        }
      }
    }
    // age: "the population is spread out with " ... "older."
    v.clear();
    if (in_sentence("the population is spread out with ", "older.", &v, &sent)) {
      bool trig = sent.empty() || ends(art_, ", ") || ends(art_, "and ");
      if (trig) {
        Census c = parse_census();
        if (c.ppl && c.P > 0) {
          double tot = 0;
          for (double x : v) tot += x;
          size_t n = v.size();
          if (ends(art_, "and ")) {
            double tgt = 100.0 - tot;
            CandMap mm;
            add_norm(mm, lattice(1, c.P, 0, c.P, 100, 1, '%', tgt - 0.05 * (n + 1) - 1e-9, tgt + 0.05 * (n + 1) + 1e-9), 0.9);
            add_norm(mm, lattice(1, c.P, 0, c.P, 100, 1, '%'), 0.1);
            open_slot(AGE_LAST, mm);
          } else {
            open_slot(AGE, lattice(1, c.P, 0, c.P, 100, 1, '%', 0, 100.0 - tot + 0.05 * (n + 1)));
          }
          return;
        }
      }
    }
  }

  CandMap km_to_mi(double A, const std::string& term) {
    CandMap d;
    double lo = std::max((A - 0.05) / KM2, 0.0), hi = (A + 0.05) / KM2;
    const int n = 400;
    for (int i = 0; i < n; i++) d[mi_str(lo + (hi - lo) * (i + 0.5) / n) + term] += 1.0 / n;
    return d;
  }

  void fire(int id, const std::smatch& m) {
    Census c = parse_census();
    CandMap mm;
    switch (id) {
      case T_AREA_MI: open_slot(AREA_MI, km_to_mi(fnum(m[1]), " ")); return;
      case T_LAND_KM: {
        double A = fnum(m[1]);
        mm[fmtd(A, 1, false) + " "] = 0.7;
        CandMap rest;
        if (range_list(rest, 0.0, A - 0.1, 1, " ", false)) add_norm(mm, rest, 0.3);
        open_slot(LAND_KM, mm);
        return;
      }
      case T_LAND_MI: open_slot(LAND_MI, km_to_mi(fnum(m[1]), " ")); return;
      case T_WATER_KM: {
        if (!c.area || !c.land) return;
        double diff = c.A - c.C;
        const double wt[3] = {0.25, 0.5, 0.25};
        for (int k = -1; k <= 1; k++) {
          double v = std::nearbyint((diff + 0.1 * k) * 10) / 10;
          if (v >= 0) mm[fmtd(v, 1, false) + " "] += wt[k + 1];
        }
        open_slot(WATER_KM, mm);
        return;
      }
      case T_WATER_MI: open_slot(WATER_MI, km_to_mi(fnum(m[1]), " ")); return;
      case T_WATER_PCT: {
        if (!c.water) return;
        auto T = iand({c.A - 0.05, c.A + 0.05}, {(c.B - 0.05) * KM2, (c.B + 0.05) * KM2});
        double ld = half_ulp(c.Ds), fd = half_ulp(c.Fws);
        auto L = iand({c.C - 0.05, c.C + 0.05}, {(c.Dm - ld) * KM2, (c.Dm + ld) * KM2});
        auto W = iand(iand({c.E - 0.05, c.E + 0.05}, {(c.Fw - fd) * KM2, (c.Fw + fd) * KM2}),
                      {T.first - L.second, T.second - L.first});
        double lo = 100 * std::max(W.first, 0.0) / T.second, hi = 100 * W.second / std::max(T.first, 1e-6);
        if (range_list(mm, lo, hi, 2, "%", false)) open_slot(WATER_PCT, mm);
        return;
      }
      case T_DENS_KM: {
        if (!c.ppl || !c.land || c.P <= 0) return;
        double ld = half_ulp(c.Ds);
        auto L = iand({c.C - 0.05, c.C + 0.05}, {(c.Dm - ld) * KM2, (c.Dm + ld) * KM2});
        double lo = c.P / L.second, hi = c.P / std::max(L.first, 1e-6);
        double sl = 0.007 * (lo + hi) / 2 + 0.06;
        if (range_list(mm, lo - sl, hi + sl, 1, "/", true)) open_slot(DENS_KM, mm);
        return;
      }
      case T_DENS_MI: {
        if (!c.ppl || !c.land || c.P <= 0) return;
        double X = fnum(m[1]);
        double ld = half_ulp(c.Ds);
        auto Lm = iand({(c.C - 0.05) / KM2, (c.C + 0.05) / KM2}, {c.Dm - ld, c.Dm + ld});
        double lo2 = c.P / Lm.second, hi2 = c.P / std::max(Lm.first, 1e-6);
        double cc = X * KM2, sl2 = 0.007 * cc + 0.2;
        double lo3 = std::max(lo2, cc - sl2), hi3 = std::min(hi2, cc + sl2);
        if (lo3 > hi3) { lo3 = cc - sl2; hi3 = cc + sl2; }
        if (range_list(mm, lo3, hi3, 1, "/", true)) open_slot(DENS_MI, mm);
        return;
      }
      case T_HDENS_KM: case T_HDENS_MI: {
        if (!c.ppl || !c.dens || c.P <= 0) return;
        std::smatch u;
        if (id == T_HDENS_KM) {
          static const std::regex hu("There (?:are|were) ([0-9][0-9,]*(?:\\.[0-9]+)?) housing units at an average density of $");
          if (!tail_match(hu, u)) return;
          double r = fnum(u[1]) / c.P;
          if (range_list(mm, (c.X - 0.05) * r - 1e-9, (c.X + 0.05) * r + 1e-9, 1, "/", true)) open_slot(HDENS_KM, mm);
        } else {
          if (!tail_match(re_hunits_, u)) return;
          double r = fnum(u[1]) / c.P;
          if (range_list(mm, (c.Y - 0.05) * r - 1e-9, (c.Y + 0.05) * r + 1e-9, 1, "/", true)) open_slot(HDENS_MI, mm);
        }
        return;
      }
      case T_HISP: {
        if (!c.ppl || c.P <= 0) return;
        if (art_.rfind("The racial makeup of the ") == std::string::npos) return;
        open_slot(HISP, lattice(1, c.P, 0, c.P, 100, 2, '%'));
        return;
      }
      case T_HH_COPY: {
        if (!c.ppl || c.H <= 0) return;
        mm[c.Hs + " "] = 1;
        open_slot(HH_COPY, mm);
        return;
      }
      case T_HHSIZE: {
        if (!c.ppl || c.H <= 0) return;
        int64_t P = c.P, H = c.H;
        add_norm(mm, lattice(1, H, P, P, 1.0, 2, ' '), 0.5);
        add_norm(mm, lattice(1, H, std::max<int64_t>(H, (int64_t)(P * 0.5)), P, 1.0, 2, ' '), 0.5);
        open_slot(HHSIZE, mm);
        return;
      }
      case T_FAMSIZE: {
        if (!c.ppl || c.F <= 0 || c.H <= 0) return;
        double hsz = fnum(m[1]);
        int64_t hpop_hi = std::min<int64_t>((int64_t)c.P, (int64_t)((hsz + 0.005) * c.H) + 1);
        open_slot(FAMSIZE, lattice(1, (int64_t)c.F, 2 * (int64_t)c.F, hpop_hi, 1.0, 2, '.'));
        return;
      }
      case T_SEX: {
        if (!c.ppl || c.P <= 1) return;
        int64_t P = c.P;
        open_slot(SEXRATIO, lattice(2, P, (int64_t)(P * 0.2), (int64_t)(P * 0.8), 100, 1, ' '));
        return;
      }
      case T_SEX18: {
        if (!c.ppl || c.P <= 1) return;
        std::vector<double> v; std::string s;
        size_t h = art_.rfind("the population is spread out with ");
        if (h == std::string::npos) return;
        static const std::regex u18("^([0-9]+\\.[0-9]+)%");
        std::string after = art_.substr(h + 34);
        std::smatch um;
        if (!std::regex_search(after, um, u18)) return;
        double u = fnum(um[1]);
        int64_t P = c.P;
        std::vector<int64_t> ks;
        int64_t k0 = (int64_t)(u * P / 100);
        char b1[32], b2[32];
        snprintf(b2, sizeof b2, "%.1f", u);
        for (int64_t k = std::max<int64_t>(0, k0 - 3); k <= k0 + 3; k++) {
          snprintf(b1, sizeof b1, "%.1f", 100.0 * k / P);
          if (!strcmp(b1, b2)) ks.push_back(k);
        }
        for (int64_t k : ks) {
          int64_t Aad = P - k;
          if (Aad > 1) add_norm(mm, lattice(2, Aad, (int64_t)(Aad * 0.2), (int64_t)(Aad * 0.8), 100, 1, ' '), 1.0 / ks.size());
        }
        open_slot(SEXRATIO18, mm);
        return;
      }
      case T_COORD1: case T_COORD2: {
        int g = id == T_COORD1 ? 1 : 5;
        double v = atof(std::string(m[g]).c_str()) + atof(std::string(m[g + 1]).c_str()) / 60 + atof(std::string(m[g + 2]).c_str()) / 3600;
        bool neg = id == T_COORD1 ? m[4] == "South" : m[8] == "West";
        double lo = v - 0.5 / 3600 - 2e-6, hi = v + 0.5 / 3600 + 2e-6;
        if (range_list(mm, lo, hi, 6, id == T_COORD1 ? "," : ")", false, 4000, neg ? "-" : ""))
          open_slot(COORD, mm);
        return;
      }
    }
  }

  void try_conv() {
    // "N unit (" or "N unit. (" with N not preceded by [0-9.,]
    const std::string& a = art_;
    if (a.size() < 4 || a[a.size() - 2] != ' ') return;
    size_t e = a.size() - 2;  // index of the space before '('
    if (e > 0 && a[e - 1] == '.') e--;
    for (auto& u : units_) {
      const std::string& un = u.first;
      if (e < un.size()) continue;
      if (a.compare(e - un.size(), un.size(), un) != 0) continue;
      size_t ne = e - un.size();
      if (ne > 0 && a[ne - 1] == ' ') ne--;
      size_t ns = ne;
      while (ns > 0 && (isdigit((unsigned char)a[ns - 1]) || a[ns - 1] == ',' || a[ns - 1] == '.')) ns--;
      // NUM must start with a digit
      while (ns < ne && !isdigit((unsigned char)a[ns])) ns++;
      if (ns >= ne) continue;
      bool neg = ns > 0 && a[ns - 1] == '-';
      if (neg) ns--;
      if (ns > 0 && (isdigit((unsigned char)a[ns - 1]) || a[ns - 1] == '.' || a[ns - 1] == ',')) continue;
      std::string num = a.substr(ns, ne - ns);
      // the number must be a full NUM token: digits/commas then optional .digits
      static const std::regex numre("^-?[0-9][0-9,]*(?:\\.[0-9]+)?$");
      if (!std::regex_match(num, numre)) continue;
      double N = fnum(num);
      bool commas = num.find(',') != std::string::npos;
      CandMap d;
      const auto& tg = u.second;
      for (auto& fo : tg) {
        double x = N * fo.first + fo.second;
        std::vector<double> vals;
        if (x == 0) vals.push_back(0.0);
        else {
          int mag = (int)std::floor(std::log10(std::fabs(x)));
          for (int sig = 1; sig <= 4; sig++) {
            double q = std::pow(10.0, mag - sig + 1);
            vals.push_back(std::nearbyint(std::nearbyint(x / q) * q * 1e6) / 1e6);
          }
          for (double q : {5.0, 50.0, 500.0}) if (std::fabs(x) >= q) vals.push_back(std::nearbyint(x / q) * q);
          for (int dec = 0; dec <= 2; dec++) { double p = std::pow(10.0, dec); vals.push_back(std::nearbyint(x * p) / p); }
        }
        std::vector<std::string> strs;
        for (double v : vals) {
          int dec = 0;
          for (; dec < 3; dec++) { double p = std::pow(10.0, dec); if (std::fabs(std::nearbyint(v * p) / p - v) < 1e-9) break; }
          strs.push_back(fmtd(v, dec, false));
          if (std::fabs(v) >= 1000) strs.push_back(fmtd(v, dec, true));
        }
        std::sort(strs.begin(), strs.end());
        strs.erase(std::unique(strs.begin(), strs.end()), strs.end());
        for (auto& s : strs) {
          d[s + " "] += 0.8 / strs.size() / tg.size();
          d[s + ")"] += 0.2 / strs.size() / tg.size();
        }
      }
      (void)commas;
      open_slot(CONV, d);
      return;
    }
  }
};

// ------------------------------------------------------------------------------------------------ final stage
// The final-stage mixer the predictor applies after its SSE (same as scripts/analysis/e12_csim.cpp): outside slots
// the record's p passes through unchanged; inside a slot, a logistic mixer over stretch(p), a bias,
// stretch(StateMap[class, q(p1), byte position, bit]) and stretch(clamped p1), weight set (class, state, bit).
class E12Mix {
 public:
  explicit E12Mix(const char* dict) : spec(dict), smp_(NCLS * 14 * 4 * 8, 0.5f), smn_(NCLS * 14 * 4 * 8, 0),
                                       w_(NCLS * 4 * 8 * 4, 0.0) {
    for (size_t s = 0; s < w_.size(); s += 4) w_[s] = 1.0;
  }
  ArithSpec spec;
  float Predict(float p_in) {
    on_ = false;
    int cls;
    if (!spec.InSlot(&cls)) return p_in;
    double pr = clampp((double)p_in);
    x_[0] = stretch(pr); x_[1] = 0.3; x_[2] = 0; x_[3] = 0;
    smi_ = -1;
    double p1; int st = 3, bp = 0, c2;
    if (spec.Predict(&p1, &c2, &st, &bp)) {
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
      for (int i = 0; i < 4; i++) w_[set_ + i] += 0.002 * err * x_[i];
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
  std::vector<float> smp_;
  std::vector<uint16_t> smn_;
  std::vector<double> w_;
  double x_[4] = {0, 0, 0, 0}, pf_ = 0.5;
  int set_ = 0, smi_ = -1, bit_ = 0;
  bool on_ = false;
};

}  // namespace e12
