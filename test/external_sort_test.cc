#include "src/core/external_sort.h"
#include "src/core/mem_monitor.h"
#include "src/core/types.h"
#include "src/core/io.h"

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
    const char* tmp = std::getenv("TEST_TMPDIR");
    test_dir_ = tmp ? std::string(tmp) + "/esort"
                    : "/tmp/atlas_esort_test";
    std::string cmd = "mkdir -p " + test_dir_;
    (void)system(cmd.c_str());
  }

  void TearDown() override {
    std::string cmd = "rm -rf " + test_dir_;
    (void)system(cmd.c_str());
  }

  // Helper to create a record with a specific timestamp
  Record MakeRecord(uint64_t ts) {
    Record r;
    std::memset(&r, 0, sizeof(r));
    r.timestamp = ts;
    r.feature_id = 42;
    return r;
  }

  // Write records to a binary file.
  std::string WriteInput(const std::vector<Record>& data) {
    std::string path = test_dir_ + "/input.bin";
    BinaryWriter writer(path);
    if (!data.empty()) {
      writer.Write(data.data(), data.size());
    }
    return path;
  }

  // Read records from a binary file.
  std::vector<Record> ReadOutput(const std::string& path) {
    std::vector<Record> result;
    // We use a small buffer for testing
    std::vector<Record> buffer(1024);
    BinaryReader reader(path, buffer.data(), buffer.size());
    while (reader.HasNext()) {
      result.push_back(reader.Peek());
      reader.Advance();
    }
    return result;
  }

  // End-to-end: write input, sort, verify output matches std::sort.
  void SortAndVerify(const std::vector<Record>& input,
                     size_t arena_mb = 1) {
    std::string in_path = WriteInput(input);
    std::string out_path = test_dir_ + "/output.bin";
    std::string tmp_dir = test_dir_ + "/runs";

    ExternalSort::Config cfg;
    cfg.input_path = in_path;
    cfg.output_path = out_path;
    cfg.temp_dir = tmp_dir;
    cfg.arena_bytes = arena_mb * 1024 * 1024;

    ExternalSort sorter(cfg);
    SortStats stats = sorter.Run();

    auto output = ReadOutput(out_path);
    auto expected = input;
    std::sort(expected.begin(), expected.end(), [](const Record& a, const Record& b) {
      return a.timestamp < b.timestamp;
    });

    ASSERT_EQ(output.size(), expected.size())
        << "count mismatch: got " << output.size()
        << " expected " << expected.size();
    
    for (size_t i = 0; i < output.size(); ++i) {
      EXPECT_EQ(output[i].timestamp, expected[i].timestamp)
          << "mismatch at index " << i;
    }
    EXPECT_EQ(stats.total_elements, input.size());
  }
};

// ─── Correctness tests ───────────────────────────────────────────────────

TEST_F(ExternalSortTest, Empty) {
  SortAndVerify({});
}

TEST_F(ExternalSortTest, SingleElement) {
  SortAndVerify({MakeRecord(42)});
}

TEST_F(ExternalSortTest, SmallSorted) {
  std::vector<Record> data;
  for (uint64_t i = 1; i <= 5; ++i) data.push_back(MakeRecord(i));
  SortAndVerify(data);
}

TEST_F(ExternalSortTest, SmallReversed) {
  std::vector<Record> data;
  for (uint64_t i = 5; i >= 1; --i) data.push_back(MakeRecord(i));
  SortAndVerify(data);
}

TEST_F(ExternalSortTest, Duplicates) {
  SortAndVerify({MakeRecord(5), MakeRecord(3), MakeRecord(5), MakeRecord(1)});
}

TEST_F(ExternalSortTest, ExtremeValues) {
  SortAndVerify({MakeRecord(UINT64_MAX), MakeRecord(0), MakeRecord(1),
                 MakeRecord(UINT64_MAX), MakeRecord(0)});
}

// ─── Multi-run tests (force small arena) ────────────────────────────────

TEST_F(ExternalSortTest, MediumRandomSingleRun) {
  std::mt19937_64 rng(42);
  std::vector<Record> data(1000); // 1000 * 128 = 128KB, fits in 4MB arena
  for (auto& x : data) x = MakeRecord(rng());
  SortAndVerify(data, /*arena_mb=*/4);
}

TEST_F(ExternalSortTest, ForcesMultipleRuns) {
  // 1 MB arena ⇒ 1MB/128B = 8192 records per run
  // 20,000 records ⇒ ~3 runs
  std::mt19937_64 rng(99);
  std::vector<Record> data(20000);
  for (auto& x : data) x = MakeRecord(rng());
  SortAndVerify(data, /*arena_mb=*/1);
}

// ─── Memory tests ─────────────────────────────────────────────────────

TEST_F(ExternalSortTest, PeakRssMonitoring) {
  // Verifies that the stats report some memory usage
  std::mt19937_64 rng(123);
  std::vector<Record> data(10000);
  for (auto& x : data) x = MakeRecord(rng());

  std::string in_path = WriteInput(data);
  std::string out_path = test_dir_ + "/mem_output.bin";
  std::string tmp_dir = test_dir_ + "/runs_mem";

  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = out_path;
  cfg.temp_dir = tmp_dir;
  cfg.arena_bytes = 4 * 1024 * 1024;

  ExternalSort sorter(cfg);
  SortStats stats = sorter.Run();

  EXPECT_GT(stats.peak_rss_kb, 0u);
}

}  // namespace
}  // namespace atlas
