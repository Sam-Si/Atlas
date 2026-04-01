#include "src/core/external_sort.h"
#include "src/core/mem_monitor.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace atlas {
namespace {

class ExternalSortTest : public ::testing::Test {
 protected:
  std::string test_dir_;

  void SetUp() override {
    // Bazel provides TEST_TMPDIR; fall back to /tmp
    const char* tmp = std::getenv("TEST_TMPDIR");
    test_dir_ = tmp ? std::string(tmp) + "/esort"
                    : "/tmp/atlas_esort_test";
    // Create dir (ok if exists)
    std::string cmd = "mkdir -p " + test_dir_;
    (void)system(cmd.c_str());
  }

  void TearDown() override {
    std::string cmd = "rm -rf " + test_dir_;
    (void)system(cmd.c_str());
  }

  // Write ints to a text file, one per line.
  std::string WriteInput(const std::vector<Element>& data) {
    std::string path = test_dir_ + "/input.txt";
    std::ofstream f(path);
    for (auto v : data) f << v << "\n";
    f.close();
    return path;
  }

  // Read ints from a text file.
  std::vector<Element> ReadOutput(const std::string& path) {
    std::vector<Element> result;
    std::ifstream f(path);
    Element v;
    while (f >> v) result.push_back(v);
    return result;
  }

  // End-to-end: write input, sort, verify output matches std::sort.
  void SortAndVerify(const std::vector<Element>& input,
                     size_t arena_mb = 1) {
    std::string in_path = WriteInput(input);
    std::string out_path = test_dir_ + "/output.txt";
    std::string tmp_dir = test_dir_ + "/runs";

    ExternalSort::Config cfg;
    cfg.input_path = in_path;
    cfg.output_path = out_path;
    cfg.temp_dir = tmp_dir;
    cfg.arena_bytes = arena_mb * 1024 * 1024;

    ExternalSort sorter(std::move(cfg));
    SortStats stats = sorter.Run();

    auto output = ReadOutput(out_path);
    auto expected = input;
    std::sort(expected.begin(), expected.end());

    ASSERT_EQ(output.size(), expected.size())
        << "count mismatch: got " << output.size()
        << " expected " << expected.size();
    EXPECT_EQ(output, expected);
    EXPECT_EQ(stats.total_elements, input.size());
  }
};

// ─── Correctness tests ───────────────────────────────────────────────────

TEST_F(ExternalSortTest, Empty) {
  SortAndVerify({});
}

TEST_F(ExternalSortTest, SingleElement) {
  SortAndVerify({42});
}

TEST_F(ExternalSortTest, SmallSorted) {
  SortAndVerify({1, 2, 3, 4, 5});
}

TEST_F(ExternalSortTest, SmallReversed) {
  SortAndVerify({5, 4, 3, 2, 1});
}

TEST_F(ExternalSortTest, NegativeNumbers) {
  SortAndVerify({-100, 50, -3, 0, 99, -1});
}

TEST_F(ExternalSortTest, Duplicates) {
  SortAndVerify({5, 3, 5, 1, 3, 5, 1});
}

TEST_F(ExternalSortTest, AllSame) {
  std::vector<Element> data(500, 7);
  SortAndVerify(data);
}

TEST_F(ExternalSortTest, ExtremeValues) {
  SortAndVerify({INT64_MAX, INT64_MIN, 0, -1, 1,
                 INT64_MAX, INT64_MIN});
}

// ─── Multi-run tests (force small arena) ────────────────────────────────

TEST_F(ExternalSortTest, MediumRandomSingleRun) {
  std::mt19937_64 rng(42);
  std::vector<Element> data(50000);
  for (auto& x : data) x = static_cast<Element>(rng());
  SortAndVerify(data, /*arena_mb=*/4);
}

TEST_F(ExternalSortTest, ForcesMultipleRuns) {
  // 1 MB arena ⇒ sort_cap = 512KB/8 = 65536 elements per run
  // 200K elements ⇒ 4 runs ⇒ tests the K-way merge path
  std::mt19937_64 rng(99);
  std::vector<Element> data(200000);
  for (auto& x : data) x = static_cast<Element>(rng());
  SortAndVerify(data, /*arena_mb=*/1);
}

TEST_F(ExternalSortTest, ManySmallRuns) {
  // 1 MB arena with 500K elements ⇒ 65536 elems/run ⇒ 8 runs
  // Tests merge with more runs than ForcesMultipleRuns (4 runs)
  std::mt19937_64 rng(7);
  std::vector<Element> data(500000);
  for (auto& x : data) x = static_cast<Element>(rng());
  SortAndVerify(data, /*arena_mb=*/1);
}

// ─── Memory tests ─────────────────────────────────────────────────────

TEST_F(ExternalSortTest, PeakRssStaysWithinBudget) {
  // Sort 500K random elements with a 4 MB arena.
  // The arena uses 4 MB of anonymous memory.  The process overhead
  // (binary, libc, stack, heap, page-cache) should add < 50 MB.
  // If posix_fadvise is broken, the input file's page cache would
  // push RSS well beyond that.
  const size_t arena_mb = 4;
  const size_t max_overhead_mb = 50;  // generous upper-bound for non-arena RSS

  std::mt19937_64 rng(123);
  std::vector<Element> data(500000);
  for (auto& x : data) x = static_cast<Element>(rng());

  std::string in_path = WriteInput(data);
  std::string out_path = test_dir_ + "/output.txt";
  std::string tmp_dir = test_dir_ + "/runs";

  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = out_path;
  cfg.temp_dir = tmp_dir;
  cfg.arena_bytes = arena_mb * 1024 * 1024;

  ExternalSort sorter(std::move(cfg));
  SortStats stats = sorter.Run();

  // Verify correctness first
  auto output = ReadOutput(out_path);
  auto expected = data;
  std::sort(expected.begin(), expected.end());
  ASSERT_EQ(output.size(), expected.size());
  EXPECT_EQ(output, expected);

  // Verify memory: peak anonymous RSS should be close to arena size
  fprintf(stderr, "  Peak RSS (VmHWM):  %zu MB\n", stats.peak_rss_kb / 1024);
  fprintf(stderr, "  Peak anon:         %zu MB\n", stats.peak_anon_kb / 1024);
  fprintf(stderr, "  Peak file (cache): %zu MB\n", stats.peak_file_kb / 1024);

  // peak_rss_kb is process-lifetime VmHWM, so it includes any earlier tests.
  // Use peak_anon_kb (our sampled high-water mark) which is more targeted.
  // arena (4 MB) + overhead (< 50 MB) should be well under 100 MB.
  EXPECT_LT(stats.peak_anon_kb / 1024, arena_mb + max_overhead_mb)
      << "Anonymous RSS exceeded arena + " << max_overhead_mb
      << " MB overhead budget";
}

TEST(MemMonitorTest, ReturnsNonZeroOnLinux) {
  atlas::MemInfo m = atlas::GetMemInfo();
  // On Linux /proc/self/status always exists for a running process
  EXPECT_GT(m.vm_rss_kb, 0u)  << "VmRSS should be > 0";
  EXPECT_GT(m.vm_hwm_kb, 0u)  << "VmHWM should be > 0";
  EXPECT_GE(m.vm_hwm_kb, m.vm_rss_kb) << "VmHWM must be >= VmRSS";
  // Our test binary should have at least some anonymous pages (stack, heap)
  EXPECT_GT(m.rss_anon_kb, 0u) << "RssAnon should be > 0";
}

}  // namespace
}  // namespace atlas
