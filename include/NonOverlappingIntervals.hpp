#pragma once

#include <concepts>
#include <cstddef>
#include <map>
#include <vector>

namespace fc {

template <typename T>
concept Comparable = std::totally_ordered<T>;

template <Comparable T> using Interval = std::pair<T, T>;

template <Comparable T> class NonOverlappingIntervals {
private:
  std::map<T, T> container_;

public:
  NonOverlappingIntervals();
  std::vector<Interval<T>>
  uncovered_intervals(const Interval<T> &interval) const;
  void add_interval(const Interval<T> &interval);
  size_t size();
  const std::map<T, T> &get_container() const; // use in building file
  ~NonOverlappingIntervals();
};

} // namespace fc
