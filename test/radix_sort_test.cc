#include "src/core/radix_sort.h"
#include "src/core/types.h"

#include <algorithm>
#include <climits>
#include <random>
#include <vector>

#include "gtest/gtest.h"

namespace atlas {
namespace {

// Helper: sorts `data` with RadixSort and checks it matches std::sort.
void ExpectSorted(std::vector<Record> data) {
  std::vector<Record> expected = data;
  std::sort(expected.begin(), expected.end(), [](const Record& a, const Record& b) {
    return a.timestamp < b.timestamp;
  });

  std::vector<Record> scratch(data.size());
  RadixSort(data.data(), scratch.data(), data.size());

  for (size_t i = 0; i < data.size(); ++i) {
    EXPECT_EQ(data[i].timestamp, expected[i].timestamp);
  }
}

Record MakeRecord(uint64_t ts) {
  Record r;
  std::memset(&r, 0, sizeof(r));
  r.timestamp = ts;
  return r;
}

TEST(RadixSortTest, Empty) {
  RadixSort(nullptr, nullptr, 0);  // no crash
}

TEST(RadixSortTest, SingleElement) {
  ExpectSorted({MakeRecord(42)});
}

TEST(RadixSortTest, TwoElements) {
  ExpectSorted({MakeRecord(7), MakeRecord(3)});
}

TEST(RadixSortTest, AlreadySorted) {
  std::vector<Record> data;
  for (uint64_t i = 1; i <= 10; ++i) data.push_back(MakeRecord(i));
  ExpectSorted(data);
}

TEST(RadixSortTest, ReverseSorted) {
  std::vector<Record> data;
  for (uint64_t i = 10; i >= 1; --i) data.push_back(MakeRecord(i));
  ExpectSorted(data);
}

TEST(RadixSortTest, AllSameValue) {
  ExpectSorted({MakeRecord(42), MakeRecord(42), MakeRecord(42)});
}

TEST(RadixSortTest, ExtremeValues) {
  ExpectSorted({
      MakeRecord(UINT64_MAX), MakeRecord(0), MakeRecord(1),
      MakeRecord(UINT64_MAX - 1), MakeRecord(UINT64_MAX)});
}

TEST(RadixSortTest, LargeRandom) {
  std::mt19937_64 rng(42);
  std::vector<Record> data(10000);
  for (auto& x : data) x = MakeRecord(rng());
  ExpectSorted(data);
}

TEST(RadixSortTest, Duplicates) {
  ExpectSorted({MakeRecord(5), MakeRecord(3), MakeRecord(5), MakeRecord(1)});
}

}  // namespace
}  // namespace atlas
