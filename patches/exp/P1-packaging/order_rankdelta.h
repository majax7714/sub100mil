// order_rankdelta.h ([sub100mil P1]): decodes .new_article_order from the rank-delta text written by
// patches/exp/P1-packaging/order_rankdelta.py back to the article ids, in order.
#ifndef ORDER_RANKDELTA_H
#define ORDER_RANKDELTA_H
#include <istream>
#include <string>
#include <vector>
inline std::vector<int> DecodeRankDeltaOrder(std::istream& in) {
  std::string line;
  std::vector<int> ids;
  if (!getline(in, line)) return ids;
  const int M = std::stoi(line);
  std::vector<int> fw(M + 1);
  for (int i = 1; i <= M; i++) fw[i] = i & -i;  // Fenwick tree over [0, M): every id unused
  int top = 1;
  while (top * 2 <= M) top *= 2;
  int prev = -1;
  while (getline(in, line)) {
    int r = std::stoi(line);
    if (prev >= 0)
      for (int i = prev; i > 0; i -= i & -i) r += fw[i];  // + unused ids below the previous one
    int pos = 0;  // the r-th (0-based) unused id
    for (int step = top; step; step >>= 1)
      if (pos + step <= M && fw[pos + step] <= r) { pos += step; r -= fw[pos]; }
    for (int i = pos + 1; i <= M; i += i & -i) fw[i]--;
    ids.push_back(pos);
    prev = pos;
  }
  return ids;
}
#endif
