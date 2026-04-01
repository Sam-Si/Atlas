#include "src/core/radix_sort.h"

#include <algorithm>
#include <climits>
#include <random>
#include <vector>

#include "gtest/gtest.h"

namespace atlas {
namespace {

// Helper: sorts `data` with RadixSort and checks it matches std::sort.
void ExpectSorted(std::vector<Element> data) {
  std::vector<Element> expected = data;
  std::sort(expected.begin(), expected.end());

  std::vector<Element> scratch(data.size());
  RadixSort(data.data(), scratch.data(), data.size());

  ASSERT_EQ(data, expected);
}

TEST(RadixSortTest, Empty) {
  RadixSort(nullptr, nullptr, 0);  // no crash
}

TEST(RadixSortTest, SingleElement) {
  ExpectSorted({42});
}

TEST(RadixSortTest, TwoElements) {
  ExpectSorted({7, 3});
}

TEST(RadixSortTest, AlreadySorted) {
  ExpectSorted({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
}

TEST(RadixSortTest, ReverseSorted) {
  ExpectSorted({10, 9, 8, 7, 6, 5, 4, 3, 2, 1});
}

TEST(RadixSortTest, AllSameValue) {
  ExpectSorted({42, 42, 42, 42, 42});
}

TEST(RadixSortTest, AllZeros) {
  ExpectSorted({0, 0, 0, 0});
}

TEST(RadixSortTest, NegativeNumbers) {
  ExpectSorted({-5, -3, -1, -4, -2});
}

TEST(RadixSortTest, MixedPositiveNegative) {
  ExpectSorted({3, -1, 4, -1, 5, -9, 2, -6, 5, 3, -5});
}

TEST(RadixSortTest, ExtremeValues) {
  ExpectSorted({
      INT64_MAX, INT64_MIN, 0, -1, 1,
      INT64_MAX - 1, INT64_MIN + 1, INT64_MAX, INT64_MIN});
}

TEST(RadixSortTest, SmallRange) {
  std::mt19937_64 rng(123);
  std::uniform_int_distribution<Element> dist(-10, 10);
  std::vector<Element> data(10000);
  for (auto& x : data) x = dist(rng);
  ExpectSorted(data);
}

TEST(RadixSortTest, LargeRandom) {
  std::mt19937_64 rng(42);
  std::vector<Element> data(500000);
  for (auto& x : data) x = static_cast<Element>(rng());
  ExpectSorted(data);
}

TEST(RadixSortTest, Duplicates) {
  ExpectSorted({5, 3, 5, 1, 3, 5, 1, 5, 3, 1});
}

}  // namespace
}  // namespace atlas
