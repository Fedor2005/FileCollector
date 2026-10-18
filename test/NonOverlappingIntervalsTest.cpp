#include "NonOverlappingIntervals.hpp"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <utility>
#include <vector>

namespace {

using IntMap = std::map<int, int>;
using Pairs = std::vector<std::pair<int, int>>;
using fc::Interval;
using fc::NonOverlappingIntervals;

// The only place that knows how Interval<T> is laid out.
template <typename T>
std::vector<std::pair<T, T>> ToPairs(const std::vector<Interval<T>> &v) {
  std::vector<std::pair<T, T>> out;
  out.reserve(v.size());
  for (const auto &iv : v)
    out.emplace_back(iv.first, iv.second);
  return out;
}

// Invariant: every interval is non-empty, intervals are sorted,
// do not overlap and do not touch (touching ones must be merged).
template <typename T>
::testing::AssertionResult IsCanonical(const std::map<T, T> &m) {
  const T *prev_end = nullptr;
  for (const auto &[s, e] : m) {
    if (!(s < e))
      return ::testing::AssertionFailure()
             << "empty interval [" << s << ", " << e << ")";
    if (prev_end && !(*prev_end < s))
      return ::testing::AssertionFailure()
             << "interval starting at " << s
             << " overlaps or touches previous end " << *prev_end;
    prev_end = &e;
  }
  return ::testing::AssertionSuccess();
}

class NonOverlappingIntervalsTest : public ::testing::Test {
protected:
  void Add(int s, int e) {
    iv.add_interval({s, e});
    ASSERT_TRUE(IsCanonical(iv.get_container()));
  }
  const IntMap &C() const { return iv.get_container(); }
  Pairs Uncovered(int s, int e) const {
    return ToPairs(iv.uncovered_intervals({s, e}));
  }

  NonOverlappingIntervals<int> iv;
};

// ---------------------------------------------------------------- add_interval

TEST_F(NonOverlappingIntervalsTest, StartsEmpty) { EXPECT_TRUE(C().empty()); }

TEST_F(NonOverlappingIntervalsTest, AddToEmpty) {
  Add(2, 5);
  EXPECT_EQ(C(), (IntMap{{2, 5}}));
}

TEST_F(NonOverlappingIntervalsTest, EmptyAndInvertedIntervalsAreIgnored) {
  EXPECT_THROW(iv.add_interval({5, 5}), fc::Exception);
  EXPECT_THROW(iv.add_interval({7, 3}), fc::Exception);
  EXPECT_TRUE(C().empty());
}

TEST_F(NonOverlappingIntervalsTest, DisjointIntervalsStaySeparateAndSorted) {
  Add(12, 15);
  Add(2, 5);
  Add(7, 9);
  EXPECT_EQ(C(), (IntMap{{2, 5}, {7, 9}, {12, 15}}));
}

TEST_F(NonOverlappingIntervalsTest, OverlapOnTheLeftMerges) {
  Add(5, 10);
  Add(2, 7);
  EXPECT_EQ(C(), (IntMap{{2, 10}}));
}

TEST_F(NonOverlappingIntervalsTest, OverlapOnTheRightMerges) {
  Add(2, 7);
  Add(5, 10);
  EXPECT_EQ(C(), (IntMap{{2, 10}}));
}

TEST_F(NonOverlappingIntervalsTest, TouchingOnTheRightMerges) {
  Add(1, 3);
  Add(3, 5);
  EXPECT_EQ(C(), (IntMap{{1, 5}}));
}

TEST_F(NonOverlappingIntervalsTest, TouchingOnTheLeftMerges) {
  Add(3, 5);
  Add(1, 3);
  EXPECT_EQ(C(), (IntMap{{1, 5}}));
}

TEST_F(NonOverlappingIntervalsTest, FillingGapBetweenTwoMergesAllThree) {
  Add(1, 3);
  Add(5, 7);
  Add(3, 5);
  EXPECT_EQ(C(), (IntMap{{1, 7}}));
}

TEST_F(NonOverlappingIntervalsTest, ContainedIntervalIsNoop) {
  Add(2, 10);
  Add(4, 6);
  Add(2, 10);
  Add(2, 4);
  Add(8, 10);
  EXPECT_EQ(C(), (IntMap{{2, 10}}));
}

TEST_F(NonOverlappingIntervalsTest, CoveringIntervalSwallowsInnerOnes) {
  Add(3, 4);
  Add(6, 7);
  Add(9, 10);
  Add(1, 20);
  EXPECT_EQ(C(), (IntMap{{1, 20}}));
}

TEST_F(NonOverlappingIntervalsTest, SameStartLongerEndExtends) {
  Add(2, 5);
  Add(2, 8);
  EXPECT_EQ(C(), (IntMap{{2, 8}}));
}

TEST_F(NonOverlappingIntervalsTest, ExampleFromDiagramMergesIntoOne) {
  Add(2, 5);
  Add(7, 9);
  Add(12, 15);
  Add(4, 12); // overlaps [2,5), covers [7,9), touches [12,15)
  EXPECT_EQ(C(), (IntMap{{2, 15}}));
}

// The right edge grows while merging; merging must stop at the next real gap
// and resume once that gap is filled.
TEST_F(NonOverlappingIntervalsTest, MergeStopsAtGapAfterRightEdgeGrows) {
  Add(7, 10);
  Add(10, 12); // touches -> already merged into [7,12)
  EXPECT_EQ(C(), (IntMap{{7, 12}}));

  NonOverlappingIntervals<int> other;
  other.add_interval({14, 16});
  other.add_interval({7, 10});
  other.add_interval({11, 14});
  other.add_interval({4, 8}); // grows to [4,10), must not merge [11,14)
  EXPECT_EQ(other.get_container(), (IntMap{{4, 10}, {11, 16}}));
  other.add_interval({10, 11}); // closes the gap
  EXPECT_EQ(other.get_container(), (IntMap{{4, 16}}));
}

TEST_F(NonOverlappingIntervalsTest, NegativeValues) {
  Add(-10, -5);
  Add(-6, 0);
  EXPECT_EQ(C(), (IntMap{{-10, 0}}));
}

// --------------------------------------------------------- uncovered_intervals

TEST_F(NonOverlappingIntervalsTest, UncoveredOnEmptyContainerIsWholeRange) {
  EXPECT_EQ(Uncovered(3, 8), (Pairs{{3, 8}}));
}

TEST_F(NonOverlappingIntervalsTest, UncoveredOfEmptyOrInvertedRangeIsEmpty) {
  Add(2, 5);
  EXPECT_TRUE(Uncovered(4, 4).empty());
  EXPECT_TRUE(Uncovered(9, 1).empty());
}

TEST_F(NonOverlappingIntervalsTest, UncoveredExampleFromDiagram) {
  Add(2, 5);
  Add(7, 9);
  Add(12, 15);
  EXPECT_EQ(Uncovered(4, 14), (Pairs{{5, 7}, {9, 12}}));
}

TEST_F(NonOverlappingIntervalsTest, UncoveredWhenFullyCoveredIsEmpty) {
  Add(2, 10);
  EXPECT_TRUE(Uncovered(2, 10).empty());
  EXPECT_TRUE(Uncovered(3, 7).empty());
}

TEST_F(NonOverlappingIntervalsTest, UncoveredRangeOutsideAllIntervals) {
  Add(10, 20);
  EXPECT_EQ(Uncovered(0, 5), (Pairs{{0, 5}}));
  EXPECT_EQ(Uncovered(25, 30), (Pairs{{25, 30}}));
}

TEST_F(NonOverlappingIntervalsTest, UncoveredRangeTouchingIntervalBoundaries) {
  Add(10, 20);
  EXPECT_EQ(Uncovered(0, 10), (Pairs{{0, 10}}));   // ends where interval starts
  EXPECT_EQ(Uncovered(20, 30), (Pairs{{20, 30}})); // starts where interval ends
}

TEST_F(NonOverlappingIntervalsTest, UncoveredStartsInsideInterval) {
  Add(2, 6);
  Add(9, 11);
  EXPECT_EQ(Uncovered(4, 10), (Pairs{{6, 9}}));
}

TEST_F(NonOverlappingIntervalsTest, UncoveredHeadAndTail) {
  Add(5, 8);
  EXPECT_EQ(Uncovered(0, 12), (Pairs{{0, 5}, {8, 12}}));
}

TEST_F(NonOverlappingIntervalsTest, AddingUncoveredPartsCoversRange) {
  Add(2, 5);
  Add(7, 9);
  for (const auto &[s, e] : Uncovered(0, 12))
    Add(s, e);
  EXPECT_TRUE(Uncovered(0, 12).empty());
  EXPECT_EQ(C(), (IntMap{{0, 12}}));
}

// ------------------------------------------------------------- other types

TEST(NonOverlappingIntervalsDouble, WorksWithFloatingPoint) {
  NonOverlappingIntervals<double> iv;
  iv.add_interval({0.5, 1.5});
  iv.add_interval({1.25, 2.75});
  EXPECT_EQ(iv.get_container(), (std::map<double, double>{{0.5, 2.75}}));

  const auto gaps = ToPairs(iv.uncovered_intervals({0.0, 3.0}));
  ASSERT_EQ(gaps.size(), 2u);
  EXPECT_DOUBLE_EQ(gaps[0].first, 0.0);
  EXPECT_DOUBLE_EQ(gaps[0].second, 0.5);
  EXPECT_DOUBLE_EQ(gaps[1].first, 2.75);
  EXPECT_DOUBLE_EQ(gaps[1].second, 3.0);
}

// ----------------------------------------------- randomized check vs brute
// force

// Model: a boolean per unit cell [x, x+1) on a small integer line.
TEST(NonOverlappingIntervalsRandom, MatchesBruteForceModel) {
  constexpr int kSize = 60;
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> pos(0, kSize);

  for (int round = 0; round < 200; ++round) {
    NonOverlappingIntervals<int> iv;
    std::vector<bool> covered(kSize, false);

    for (int op = 0; op < 25; ++op) {
      int a = pos(rng), b = pos(rng);
      if (a > b)
        std::swap(a, b);
      if (a == b)
        continue;
      iv.add_interval({a, b});
      for (int x = a; x < b; ++x)
        covered[x] = true;

      ASSERT_TRUE(IsCanonical(iv.get_container()))
          << "round " << round << " op " << op;

      // Expected container: maximal runs of covered cells.
      IntMap expected_map;
      for (int x = 0; x < kSize;) {
        if (!covered[x]) {
          ++x;
          continue;
        }
        int s = x;
        while (x < kSize && covered[x])
          ++x;
        expected_map.emplace(s, x);
      }
      ASSERT_EQ(iv.get_container(), expected_map)
          << "round " << round << " op " << op;

      // Expected gaps for a random query.
      int qs = pos(rng), qe = pos(rng);
      Pairs expected_gaps;
      for (int x = qs; x < qe;) {
        if (covered[x]) {
          ++x;
          continue;
        }
        int s = x;
        while (x < qe && !covered[x])
          ++x;
        expected_gaps.emplace_back(s, x);
      }
      ASSERT_EQ(ToPairs(iv.uncovered_intervals({qs, qe})), expected_gaps)
          << "query [" << qs << ", " << qe << ")";
    }
  }
}

} // namespace
