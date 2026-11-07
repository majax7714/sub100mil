// e12_re.h: a tiny backtracking matcher that replaces std::regex in E12's arith specialist (P1 packaging).
// std::regex costs about 71 KB of .text (29 KB after xz) in the C3 binary; this is a few KB.
// Supported subset (anything else aborts at compile time): literal bytes, \x escapes, [..] classes with
// ranges, (..) capture groups, (?:..) groups, |, greedy * + ? on a literal/class, ? on a group, ^ and $
// (whole-input anchors, i.e. ECMAScript without multiline). Matching is depth-first with ECMAScript's
// priority order (leftmost start; alternatives left to right; greedy quantifiers longest first), so it
// returns the same match and captures as libstdc++'s std::regex (ECMAScript, DFS executor) on this subset.
// Captures are copied out, so a Match never refers to the searched string.
#ifndef E12_RE_H
#define E12_RE_H
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace e12re {

struct Match {
  int b[10], e[10];
  std::string s[10];
  std::string operator[](int k) const { return s[k]; }
  long position(int k) const { return b[k]; }
  long length(int k) const { return e[k] - b[k]; }
};

class Re {
  struct N {
    char t = 0;  // 'c' byte/class, 'g' group, 'e' group end, '^', '$'
    char q = 0;  // 0, '*', '+', '?'
    int cap = -1;
    unsigned long long cl[4] = {0, 0, 0, 0};
    std::vector<N*> alts;
    N* next = nullptr;
    bool has(unsigned char ch) const { return (cl[ch >> 6] >> (ch & 63)) & 1; }
  };
  std::vector<std::unique_ptr<N>> pool_;
  N* root_ = nullptr;
  int ncap_ = 1;
  // match state
  const char* s_ = nullptr;
  int n_ = 0, end_ = 0;
  int cb_[10], ce_[10], pend_[10];

  N* mk(char t) { pool_.emplace_back(new N); pool_.back()->t = t; return pool_.back().get(); }
  [[noreturn]] static void bad(const std::string& p) { fprintf(stderr, "e12re: unsupported pattern %s\n", p.c_str()); exit(1); }

  // parses alternatives until ')' or end; returns the alternatives' first nodes, every one chained to `tail`
  std::vector<N*> parse_alts(const std::string& p, size_t& i, N* tail) {
    std::vector<N*> alts;
    for (;;) {
      std::vector<N*> seq;
      while (i < p.size() && p[i] != '|' && p[i] != ')') {
        char ch = p[i];
        N* n;
        if (ch == '(') {
          n = mk('g');
          i++;
          if (p.compare(i, 2, "?:") == 0) i += 2;
          else { n->cap = ncap_++; if (ncap_ > 10) bad(p); }
          N* end = mk('e');
          end->cap = n->cap;
          n->alts = parse_alts(p, i, end);
          if (i >= p.size() || p[i] != ')') bad(p);
          i++;
          n->next = end;  // the group end's successor is set when the sequence is chained
        } else if (ch == '[') {
          n = mk('c');
          i++;
          bool neg = i < p.size() && p[i] == '^';
          if (neg) i++;
          while (i < p.size() && p[i] != ']') {
            unsigned char a = (unsigned char)p[i++];
            if (a == '\\') { if (i >= p.size()) bad(p); a = (unsigned char)p[i++]; }
            unsigned char z = a;
            if (i + 1 < p.size() && p[i] == '-' && p[i + 1] != ']') { z = (unsigned char)p[i + 1]; i += 2; }
            for (int c = a; c <= z; c++) n->cl[c >> 6] |= 1ull << (c & 63);
          }
          if (i >= p.size()) bad(p);
          i++;
          if (neg) for (auto& w : n->cl) w = ~w;
        } else if (ch == '^' || ch == '$') {
          n = mk(ch);
          i++;
        } else if (ch == '*' || ch == '+' || ch == '?' || ch == '.' || ch == '{' || ch == '}' || ch == ']') {
          bad(p);
        } else {
          n = mk('c');
          unsigned char a = (unsigned char)ch;
          i++;
          if (a == '\\') {
            if (i >= p.size()) bad(p);
            a = (unsigned char)p[i++];
            if (isalnum(a)) bad(p);  // \d, \b, \n ... are not supported
          }
          n->cl[a >> 6] |= 1ull << (a & 63);
        }
        if (i < p.size() && (p[i] == '*' || p[i] == '+' || p[i] == '?')) {
          n->q = p[i++];
          if (n->t == 'g' && n->q != '?') bad(p);
          if (n->t == '^' || n->t == '$') bad(p);
          if (i < p.size() && (p[i] == '?' || p[i] == '*' || p[i] == '+')) bad(p);  // lazy / stacked
        }
        seq.push_back(n);
      }
      // chain the sequence
      N* first = tail;
      for (size_t k = seq.size(); k-- > 0;) {
        N* n = seq[k];
        if (n->t == 'g') n->next->next = first, n->next = first;  // group end -> successor; group skip -> successor
        else n->next = first;
        first = n;
      }
      alts.push_back(first);
      if (i < p.size() && p[i] == '|') { i++; continue; }
      return alts;
    }
  }

  bool m(const N* n, int p) {
    if (!n) { end_ = p; return true; }
    switch (n->t) {
      case 'c': {
        if (n->q == 0) return p < n_ && n->has((unsigned char)s_[p]) && m(n->next, p + 1);
        if (n->q == '?') return (p < n_ && n->has((unsigned char)s_[p]) && m(n->next, p + 1)) || m(n->next, p);
        int k = 0;
        while (p + k < n_ && n->has((unsigned char)s_[p + k])) k++;
        for (int j = k; j >= (n->q == '+' ? 1 : 0); j--)
          if (m(n->next, p + j)) return true;
        return false;
      }
      case '^': return p == 0 && m(n->next, p);
      case '$': return p == n_ && m(n->next, p);
      case 'g': {
        for (const N* a : n->alts) {
          int sv = n->cap >= 0 ? pend_[n->cap] : 0;
          if (n->cap >= 0) pend_[n->cap] = p;
          if (m(a, p)) return true;
          if (n->cap >= 0) pend_[n->cap] = sv;
        }
        return n->q == '?' && m(n->next, p);
      }
      case 'e': {
        if (n->cap < 0) return m(n->next, p);
        int ob = cb_[n->cap], oe = ce_[n->cap];
        cb_[n->cap] = pend_[n->cap];
        ce_[n->cap] = p;
        if (m(n->next, p)) return true;
        cb_[n->cap] = ob;
        ce_[n->cap] = oe;
        return false;
      }
    }
    return false;
  }

 public:
  Re() = default;
  explicit Re(const std::string& p) {
    size_t i = 0;
    N* g = mk('g');  // the whole pattern is group 0
    g->cap = 0;
    N* end = mk('e');
    end->cap = 0;
    g->alts = parse_alts(p, i, end);
    if (i != p.size()) bad(p);
    g->next = end;
    root_ = g;
  }
  // first match in s[from..] (positions relative to s); like std::regex_search on [s.begin()+from, s.end())
  // with positions reported from s.begin() + from.
  bool search(const std::string& s, Match& mt, size_t from = 0) {
    s_ = s.data() + from;
    n_ = (int)(s.size() - from);
    for (int st = 0; st <= n_; st++) {
      for (int k = 0; k < ncap_; k++) cb_[k] = ce_[k] = pend_[k] = -1;
      if (m(root_, st)) {
        for (int k = 0; k < 10; k++) {
          if (k < ncap_ && cb_[k] >= 0) { mt.b[k] = cb_[k]; mt.e[k] = ce_[k]; mt.s[k].assign(s_ + cb_[k], ce_[k] - cb_[k]); }
          else { mt.b[k] = mt.e[k] = -1; mt.s[k].clear(); }
        }
        return true;
      }
    }
    return false;
  }
  // every non-overlapping match, like std::sregex_iterator (patterns that cannot match empty only)
  template <class F> void each(const std::string& s, F f) {
    Match mt;
    size_t from = 0;
    while (from <= s.size() && search(s, mt, from)) {
      f(mt);
      if (mt.e[0] == mt.b[0]) { fprintf(stderr, "e12re: empty match in each()\n"); exit(1); }
      from += mt.e[0];
    }
  }
};

}  // namespace e12re
#endif
