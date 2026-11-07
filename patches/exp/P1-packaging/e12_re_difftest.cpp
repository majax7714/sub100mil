// e12_re_difftest.cpp: differential test of e12re::Re against std::regex (ECMAScript) on E12's patterns over enwik9 text
// windows (every trigger point: tails of 400 B, percent lists, u18, number tokens; article-length windows every 97th).
// g++ -O2 -std=c++17 -I patches/exp/P1-packaging -o difftest e12_re_difftest.cpp; ./difftest <files...>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include "e12_re.h"
static const char* NUM = "([0-9][0-9,]*(?:\\.[0-9]+)?)";
static long nt = 0, nm = 0, bad = 0;
static void cmp1(std::regex& sr, e12re::Re& er, const std::string& s, const std::string& pat, bool full = false) {
  std::smatch m; e12re::Match e;
  bool a = full ? std::regex_match(s, m, sr) : std::regex_search(s, m, sr);
  bool b = er.search(s, e);
  nt++;
  bool ok = a == b;
  if (ok && a) {
    nm++;
    for (size_t k = 0; k < m.size() && k < 10; k++) {
      if (m[k].matched != (e.b[k] >= 0)) ok = false;
      else if (m[k].matched && (m.position(k) != e.position(k) || m.length(k) != e.length(k) || m[k].str() != e[k])) ok = false;
    }
  }
  if (!ok && bad++ < 10) std::cerr << "MISMATCH pat=" << pat << " a=" << a << " b=" << b << " s=[" << s.substr(s.size() > 120 ? s.size() - 120 : 0) << "]\n";
}
static void cmpit(std::regex& sr, e12re::Re& er, const std::string& s) {
  std::vector<std::string> A, B;
  for (auto it = std::sregex_iterator(s.begin(), s.end(), sr); it != std::sregex_iterator(); ++it) A.push_back((*it)[1]);
  er.each(s, [&](const e12re::Match& m) { B.push_back(m[1]); });
  nt++; if (A != B && bad++ < 10) std::cerr << "ITER MISMATCH\n";
  if (!A.empty()) nm++;
}
int main(int argc, char** argv) {
  std::string N = NUM, DEG = "&&\xc2\xb0";
  std::vector<std::string> trig = {
        "has a total area of " + N + " \\[square kilometer\\|km&&sup2;\\] \\($",
        "has a total area of " + N + " \\[square kilometer\\|km&&sup2;\\] \\(" + N + " \\[square mile\\|mi&&sup2;\\]\\)\\.  $",
        "\\[square mile\\|mi&&sup2;\\]\\)\\.  " + N + " km&&sup2; \\($",
        " mi&&sup2;\\) of it is land and $",
        " of it is land and " + N + " km&&sup2; \\($",
        " mi&&sup2;\\) of it is water\\.  The total area is $",
        "The \\[population density\\] is $",
        "The \\[population density\\] is " + N + "/km&&sup2; \\($",
        "housing units at an average density of $",
        "housing units at an average density of " + N + "/km&&sup2; \\($",
        "races\\.  $",
        "\n\nThere (?:are|were) $",
        "The average household size is $",
        "The average household size is " + N + " and the average family size is $",
        "For every 100 females there (?:are|were) $",
        "For every 100 females age 18 and over, there (?:are|were) $",
        "is located at ([0-9]+)" + DEG + "([0-9]+)'([0-9]+)\" (North|South), ([0-9]+)" + DEG + "([0-9]+)'([0-9]+)\" (West|East) \\($",
        "is located at ([0-9]+)" + DEG + "([0-9]+)'([0-9]+)\" (North|South), ([0-9]+)" + DEG + "([0-9]+)'([0-9]+)\" (West|East) \\((-?[0-9]+\\.[0-9]+), $",
        "There (?:are|were) ([0-9][0-9,]*(?:\\.[0-9]+)?) housing units at an average density of $",
        "There (?:are|were) " + N + " housing units at an average density of " + N + "/km&&sup2; \\($",
  };
  std::vector<std::string> art = {
    "there (?:are|were) " + N + " people, " + N + " households, and " + N + " families",
    "has a total area of " + N + " \\[square kilometer\\|km&&sup2;\\] \\(" + N + " \\[square mile\\|mi&&sup2;\\]\\)\\.",
    "\\)\\.  " + N + " km&&sup2; \\(" + N + " mi&&sup2;\\) of it is land",
    " of it is land and " + N + " km&&sup2; \\(" + N + " mi&&sup2;\\) of it is water",
    "The \\[population density\\] is " + N + "/km&&sup2; \\(" + N + "/mi&&sup2;\\)",
  };
  auto R = [](const std::string& s) { return std::regex(s, std::regex::ECMAScript | std::regex::optimize); };
  std::vector<std::regex> ts, as; std::vector<e12re::Re> te, ae;
  for (auto& p : trig) { ts.push_back(R(p)); te.emplace_back(p); }
  for (auto& p : art) { as.push_back(R(p)); ae.emplace_back(p); }
  std::regex pct_s("([0-9]+\\.[0-9]+)%"); e12re::Re pct_e("([0-9]+\\.[0-9]+)%");
  std::regex u18_s("^([0-9]+\\.[0-9]+)%"); e12re::Re u18_e("^([0-9]+\\.[0-9]+)%");
  std::regex num_s("^-?[0-9][0-9,]*(?:\\.[0-9]+)?$"); e12re::Re num_e("^-?[0-9][0-9,]*(?:\\.[0-9]+)?$");
  for (int f = 1; f < argc; f++) {
    std::ifstream in(argv[f], std::ios::binary); std::stringstream ss; ss << in.rdbuf(); std::string t = ss.str();
    auto rep = [&](const std::string& a, const std::string& b) { size_t p = 0; std::string o; o.reserve(t.size());
      for (size_t q; (q = t.find(a, p)) != std::string::npos; p = q + a.size()) o.append(t, p, q - p).append(b);
      o.append(t, p, std::string::npos); t.swap(o); };
    rep("&quot;", "\""); rep("&amp;", "&&"); rep("&lt;", "<"); rep("&gt;", ">");
    size_t page = 0; long k = 0;
    for (size_t i = 1; i < t.size(); i++) {
      if (t.compare(i - 1 < 5 ? 0 : i - 6, 6, "<page>") == 0) page = i - 6;
      char c = t[i - 1];
      if (c != ' ' && c != '(' && c != '\n') continue;
      std::string tail = t.substr(i > 400 ? i - 400 : 0, i > 400 ? 400 : i);
      for (size_t j = 0; j < ts.size(); j++) cmp1(ts[j], te[j], tail, trig[j]);
      std::string tl = t.substr(i > 40 ? i - 40 : 0, i > 40 ? 40 : i);
      cmp1(u18_s, u18_e, t.substr(i, 12), "u18");
      cmpit(pct_s, pct_e, tail);
      // numbers: the token before the space
      size_t s0 = tl.find_last_of(" (", tl.size() - 2); std::string tok = tl.substr(s0 == std::string::npos ? 0 : s0 + 1, tl.size() - 1 - (s0 == std::string::npos ? 0 : s0 + 1));
      cmp1(num_s, num_e, tok, "num", true);
      if ((++k % 97) == 0) {
        std::string a = t.substr(page, i - page);
        if (a.size() > 20000) a = a.substr(a.size() - 20000);
        for (size_t j = 0; j < as.size(); j++) cmp1(as[j], ae[j], a, art[j]);
      }
    }
  }
  std::cout << "tests " << nt << " matched " << nm << " mismatches " << bad << "\n";
  return bad != 0;
}
