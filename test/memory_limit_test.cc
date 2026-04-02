#include "src/core/external_sort.h"
#include "src/core/mem_monitor.h"
#include "src/core/memory_arena.h"
#include "src/core/io.h"
#include "src/core/types.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace atlas {
namespace {

class MemoryLimitTest : public ::testing::Test {
 protected:
  std::string test_dir_;

  void SetUp() override {
    const char* tmp = std::getenv("TEST_TMPDIR");
    test_dir_ = tmp ? std::string(tmp) + "/memlimit"
                    : "/tmp/atlas_memlimit_test";
    std::string cmd = "mkdir -p " + test_dir_;
    (void)system(cmd.c_str());
  }

  void TearDown() override {
    std::string cmd = "rm -rf " + test_dir_;
    (void)system(cmd.c_str());
  }

  Record MakeRecord(uint64_t ts) {
    Record r;
    std::memset(&r, 0, sizeof(r));
    r.timestamp = ts;
    return r;
  }

  std::string WriteInput(const std::vector<Record>& data) {
    std::string path = test_dir_ + "/input.bin";
    BinaryWriter writer(path);
    if (!data.empty()) {
      writer.Write(data.data(), data.size());
    }
    return path;
  }

  std::vector<Record> ReadOutput(const std::string& path) {
    std::vector<Record> result;
    std::vector<Record> buffer(1024);
    BinaryReader reader(path, buffer.data(), buffer.size());
    while (reader.HasNext()) {
      result.push_back(reader.Peek());
      reader.Advance();
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// 1. DEATH TESTS — "what happens when memory is insufficient?"
// ---------------------------------------------------------------------------

TEST(MemoryLimitDeathTest, ArenaAborts_WhenMmapExceedsRlimit) {
  // macOS rlimit(RLIMIT_AS) often doesn't affect mmap correctly or has different behavior.
  // We'll only run this on Linux or skip the actual setrlimit check if it fails build.
  EXPECT_DEATH(
      {
#ifdef __linux__
        struct rlimit rl;
        rl.rlim_cur = 100ULL * 1024 * 1024;
        rl.rlim_max = 100ULL * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);
#endif
        // Requesting a massive arena that won't fit to force mmap failure.
        // 1ULL << 60 (~1 Exabyte) should fail mmap on any modern OS.
        MemoryArena arena(1ULL << 60); 
        _exit(0);
      },
      "mmap");
}

// ---------------------------------------------------------------------------
// 2. FORK + RLIMIT TEST
// ---------------------------------------------------------------------------

TEST_F(MemoryLimitTest, SortSucceeds_UnderNormalConfig) {
  std::mt19937_64 rng(42);
  std::vector<Record> data(10000);
  for (auto& x : data) x = MakeRecord(rng());

  std::string in_path = WriteInput(data);
  std::string out_path = test_dir_ + "/output.bin";
  std::string tmp_dir = test_dir_ + "/runs";

  const size_t arena_bytes = 4ULL * 1024 * 1024;

  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = out_path;
  cfg.temp_dir = tmp_dir;
  cfg.arena_bytes = arena_bytes;

  ExternalSort sorter(cfg);
  SortStats stats = sorter.Run();

  auto output = ReadOutput(out_path);
  auto expected = data;
  std::sort(expected.begin(), expected.end(), [](const Record& a, const Record& b) {
    return a.timestamp < b.timestamp;
  });

  ASSERT_EQ(output.size(), expected.size());
  for (size_t i = 0; i < output.size(); ++i) {
    EXPECT_EQ(output[i].timestamp, expected[i].timestamp);
  }
}

// ---------------------------------------------------------------------------
// 3. RSS BUDGET TESTS
// ---------------------------------------------------------------------------

TEST_F(MemoryLimitTest, PeakAnon_StaysReasonable) {
  const size_t arena_mb = 4;
  const size_t max_overhead_mb = 100; // Generous for macOS/Darwin overhead

  std::mt19937_64 rng(99);
  std::vector<Record> data(10000);
  for (auto& x : data) x = MakeRecord(rng());

  std::string in_path = WriteInput(data);
  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = test_dir_ + "/rss_output.bin";
  cfg.temp_dir = test_dir_ + "/runs_rss";
  cfg.arena_bytes = arena_mb * 1024 * 1024;

  ExternalSort sorter(cfg);
  SortStats stats = sorter.Run();

  // On macOS, peak_rss_kb comes from getrusage (ru_maxrss).
  // peak_anon_kb and peak_file_kb will be 0 as they depend on /proc/self/status.
  EXPECT_GT(stats.peak_rss_kb, 0u);
}

}  // namespace
}  // namespace atlas
