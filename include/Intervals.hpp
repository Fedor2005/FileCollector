#pragma once

#include <concepts>
#include <map>
#include <vector>

namespace fc {

template <typename T>
concept Comparable = requires(const T &a, const T &b) {
  { a < b } -> std::convertible_to<bool>;
  { a > b } -> std::convertible_to<bool>;
  { a <= b } -> std::convertible_to<bool>;
  { a >= b } -> std::convertible_to<bool>;
};

template <Comparable T> using Interval = std::pair<T, T>;

template <Comparable T> class NonOverlappingIntervals {
private:
  std::map<T, T> container_;

public:
  NonOverlappingIntervals();
  std::vector<Interval<T>>
  uncovered_intervals(const Interval<T> &interval) const;
  void add_interval(const Interval<T> &interval);
  ~NonOverlappingIntervals();
};

} // namespace fc

#include "../src/NonOverlappingIntervals.tpp"
