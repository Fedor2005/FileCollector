#include "Exception.hpp"
#include "NonOverlappingIntervals.hpp"

namespace fc {

template <Comparable T>
std::vector<Interval<T>> NonOverlappingIntervals<T>::uncovered_intervals(
    const Interval<T> &interval) const {
  auto start = interval.first;
  auto end = interval.second;
  std::vector<Interval<T>> result;
  if (!(start < end))
    return result;

  auto it = container_.lower_bound(start);
  if (it != container_.begin()) {
    auto prev = std::prev(it);
    if (start < prev->second)
      it = prev;
  }

  T cur = start;
  for (; it != container_.end() && it->first < end; ++it) {
    const T &s = it->first;
    const T &e = it->second;
    if (cur < s)
      result.emplace_back(cur, s);
    if (cur < e)
      cur = e;
    if (!(cur < end))
      break;
  }

  if (cur < end)
    result.emplace_back(cur, end);
  return result;
}

template <Comparable T>
void NonOverlappingIntervals<T>::add_interval(const Interval<T> &interval) {

  auto start = interval.first;
  auto end = interval.second;
  if (!(start < end)) {
    throw Exception(ErrorCode::IncorrectIntervalBoundaries,
                    "interval invalid with boundaries");
  }

  T new_start = start;
  T new_end = end;

  auto it = container_.lower_bound(start);

  if (it != container_.begin()) {
    auto prev = std::prev(it);
    if (!(prev->second < start)) {
      if (!(prev->second < end))
        return; // already fully covered, nothing to do
      it = prev;
    }
  }

  while (it != container_.end() && !(new_end < it->first)) {
    if (it->first < new_start)
      new_start = it->first;
    if (new_end < it->second)
      new_end = it->second;
    it = container_.erase(it);
  }

  container_.emplace_hint(it, new_start, new_end);
}

} // namespace fc
