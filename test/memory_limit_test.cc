// ═══════════════════════════════════════════════════════════════════════════
// memory_limit_test.cc — Prove that Atlas fails clearly when memory is
// insufficient and stays within budget when memory is sufficient.
//
// Three categories of tests:
//
//   1. DEATH TESTS  — Use setrlimit(RLIMIT_AS) to cap the process's virtual
//      address space, then try to allocate/sort.  GoogleTest's EXPECT_DEATH
//      forks a child for safety, so the rlimit never affects the parent.
//      These prove the code *aborts with a clear message* when OOM.
//
//   2. FORK+RLIMIT TESTS — Fork a child, set a tight RLIMIT_AS, run a sort.
//      Parent checks exit code.  Proves the sort doesn't secretly allocate
//      more memory than its arena + a small overhead.
//
//   3. RSS BUDGET TESTS — Run the sort normally, then check peak_anon_kb
//      and peak_file_kb from /proc/self/status.  Proves fadvise is working
//      and the sort stays within the declared budget.
// ═══════════════════════════════════════════════════════════════════════════

#include "src/core/external_sort.h"
#include "src/core/mem_monitor.h"
#include "src/core/memory_arena.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace atlas {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

  std::string WriteInput(const std::vector<Element>& data) {
    std::string path = test_dir_ + "/input.txt";
    std::ofstream f(path);
    for (auto v : data) f << v << "\n";
    return path;
  }

  std::vector<Element> ReadOutput(const std::string& path) {
    std::vector<Element> result;
    std::ifstream f(path);
    Element v;
    while (f >> v) result.push_back(v);
    return result;
  }
};

// ---------------------------------------------------------------------------
// 1. DEATH TESTS — "what happens when memory is insufficient?"
//
//    These use EXPECT_DEATH which forks a child process.  The setrlimit()
//    call only affects the child, so the parent test process is safe.
// ---------------------------------------------------------------------------

TEST(MemoryLimitDeathTest, ArenaAborts_WhenMmapExceedsRlimit) {
  // Scenario: Only 100 MB of virtual address space.  The process itself
  // uses ~30-50 MB, leaving ~50-70 MB free.  Requesting a 384 MB arena
  // is impossible → mmap fails → MemoryArena aborts with "mmap" message.
  //
  // This is the "gave it 1 MB of RAM" scenario — there simply isn't
  // enough address space for the arena.
  EXPECT_DEATH(
      {
        struct rlimit rl;
        rl.rlim_cur = 100ULL * 1024 * 1024;
        rl.rlim_max = 100ULL * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);

        // This mmap(384 MB) must fail → prints "mmap(...) failed" → abort()
        MemoryArena arena(384ULL * 1024 * 1024);

        // Should never reach here.
        _exit(0);
      },
      "mmap");
}

TEST(MemoryLimitDeathTest, SortAborts_WhenArenaCannotBeAllocated) {
  // Scenario: Enough address space for the binary but not for ANY arena.
  // Set RLIMIT_AS so only ~1 MB is free after the binary's own mappings.
  // Even a 4 MB arena allocation will fail.
  EXPECT_DEATH(
      {
        // Read current virtual memory size from /proc/self/status
        size_t current_vm_kb = 0;
        FILE* f = fopen("/proc/self/status", "r");
        if (f) {
          char line[256];
          while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "VmSize: %zu kB", &current_vm_kb) == 1) break;
          }
          fclose(f);
        }

        // Allow only 1 MB beyond current usage — not enough for any arena
        size_t limit = (current_vm_kb + 1024) * 1024;  // +1 MB headroom
        struct rlimit rl;
        rl.rlim_cur = limit;
        rl.rlim_max = limit;
        setrlimit(RLIMIT_AS, &rl);

        // Even a 4 MB arena can't fit
        MemoryArena arena(4ULL * 1024 * 1024);
        _exit(0);
      },
      "mmap");
}

TEST(MemoryLimitDeathTest, ExternalSort_Aborts_WithInsufficientMemory) {
  // Scenario: Construct ExternalSort with a 384 MB arena when only
  // 100 MB of address space is available.  The constructor calls
  // MemoryArena(384 MB) which fails.
  EXPECT_DEATH(
      {
        struct rlimit rl;
        rl.rlim_cur = 100ULL * 1024 * 1024;
        rl.rlim_max = 100ULL * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);

        ExternalSort::Config cfg;
        cfg.input_path = "/dev/null";
        cfg.output_path = "/dev/null";
        cfg.arena_bytes = 384ULL * 1024 * 1024;

        // Constructor allocates the arena → mmap fails → abort
        ExternalSort sorter(std::move(cfg));
        _exit(0);
      },
      "mmap");
}

// ---------------------------------------------------------------------------
// 2. FORK + RLIMIT TEST — "does the sort stay within a declared budget?"
//
//    Fork a child, set RLIMIT_AS to (current_vm + arena + small_overhead),
//    run the sort.  If the sort allocates more than declared → mmap/brk/sbrk
//    fails → child crashes → test fails.
// ---------------------------------------------------------------------------

TEST_F(MemoryLimitTest, SortSucceeds_UnderTightRlimit) {
  // Generate test data in the parent (this memory doesn't count against
  // the child's RLIMIT_AS since the child will open the file from disk).
  std::mt19937_64 rng(42);
  std::vector<Element> data(100000);
  for (auto& x : data) x = static_cast<Element>(rng());

  std::string in_path = WriteInput(data);
  std::string out_path = test_dir_ + "/output.txt";
  std::string tmp_dir = test_dir_ + "/runs";

  pid_t pid = fork();
  ASSERT_NE(pid, -1) << "fork() failed: " << strerror(errno);

  if (pid == 0) {
    // ── Child process ──────────────────────────────────────────────
    // Read our current virtual memory size.
    size_t current_vm_kb = 0;
    FILE* f = fopen("/proc/self/status", "r");
    if (f) {
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmSize: %zu kB", &current_vm_kb) == 1) break;
      }
      fclose(f);
    }

    // Budget: current VM + 4 MB arena + 30 MB overhead.
    // If the sort secretly allocates more, mmap/brk will fail.
    const size_t arena_bytes = 4ULL * 1024 * 1024;
    const size_t overhead    = 30ULL * 1024 * 1024;
    size_t limit = current_vm_kb * 1024 + arena_bytes + overhead;
    struct rlimit rl = {limit, limit};
    setrlimit(RLIMIT_AS, &rl);

    ExternalSort::Config cfg;
    cfg.input_path = in_path;
    cfg.output_path = out_path;
    cfg.temp_dir = tmp_dir;
    cfg.arena_bytes = arena_bytes;

    ExternalSort sorter(std::move(cfg));
    SortStats stats = sorter.Run();
    (void)stats;

    _exit(0);  // Success
  }

  // ── Parent: wait for child ──────────────────────────────────────
  int status = 0;
  waitpid(pid, &status, 0);

  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0)
      << "Sort child process failed under tight RLIMIT_AS!\n"
      << "  This means the sort allocated more memory than:\n"
      << "    current_vm + 4 MB arena + 30 MB overhead\n"
      << "  Exit code: "
      << (WIFEXITED(status) ? WEXITSTATUS(status) : -1)
      << ", signal: "
      << (WIFSIGNALED(status) ? WTERMSIG(status) : 0);

  // Verify correctness of the output
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    auto output = ReadOutput(out_path);
    auto expected = data;
    std::sort(expected.begin(), expected.end());
    ASSERT_EQ(output.size(), expected.size());
    EXPECT_EQ(output, expected);
  }
}

// ---------------------------------------------------------------------------
// 3. RSS BUDGET TESTS — "does /proc/self/status confirm our claims?"
// ---------------------------------------------------------------------------

TEST_F(MemoryLimitTest, PeakAnon_NeverExceeds_ArenaPlusOverhead) {
  // Sort 300K elements with a 4 MB arena.
  // Anonymous RSS = arena + heap + stack + libc.
  // Should be < 4 MB + 50 MB overhead.
  const size_t arena_mb = 4;
  const size_t max_overhead_mb = 50;

  std::mt19937_64 rng(99);
  std::vector<Element> data(300000);
  for (auto& x : data) x = static_cast<Element>(rng());

  std::string in_path = WriteInput(data);
  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = test_dir_ + "/output.txt";
  cfg.temp_dir = test_dir_ + "/runs";
  cfg.arena_bytes = arena_mb * 1024 * 1024;

  ExternalSort sorter(std::move(cfg));
  SortStats stats = sorter.Run();

  fprintf(stderr,
          "  [budget check] peak_anon=%zu MB, limit=%zu MB (arena %zu + overhead %zu)\n",
          stats.peak_anon_kb / 1024, arena_mb + max_overhead_mb,
          arena_mb, max_overhead_mb);

  EXPECT_LT(stats.peak_anon_kb / 1024, arena_mb + max_overhead_mb)
      << "FAILED: Anonymous RSS (" << stats.peak_anon_kb / 1024
      << " MB) exceeded arena (" << arena_mb
      << " MB) + overhead (" << max_overhead_mb << " MB).\n"
      << "This means the sort is allocating more anonymous memory than declared.";
}

TEST_F(MemoryLimitTest, PageCache_StaysSmall_ThanksToPosixFadvise) {
  // Sort 500K elements with a 4 MB arena.
  // The text input is ~10 MB + binary run files ~4 MB + text output ~10 MB.
  // Without posix_fadvise, all ~24 MB would sit in page cache.
  // WITH fadvise, file-backed RSS should stay well under 20 MB.
  std::mt19937_64 rng(77);
  std::vector<Element> data(500000);
  for (auto& x : data) x = static_cast<Element>(rng());

  std::string in_path = WriteInput(data);
  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = test_dir_ + "/output.txt";
  cfg.temp_dir = test_dir_ + "/runs";
  cfg.arena_bytes = 4 * 1024 * 1024;

  ExternalSort sorter(std::move(cfg));
  SortStats stats = sorter.Run();

  fprintf(stderr,
          "  [fadvise check] peak_file=%zu MB (should be < 20 MB)\n",
          stats.peak_file_kb / 1024);

  EXPECT_LT(stats.peak_file_kb / 1024, 20u)
      << "FAILED: File-backed RSS (" << stats.peak_file_kb / 1024
      << " MB) is too high.\n"
      << "This means posix_fadvise(FADV_DONTNEED) is not working — the kernel\n"
      << "is keeping file pages in the page cache, which would blow the cgroup limit.";
}

TEST_F(MemoryLimitTest, TotalRss_StaysUnder500MB_ForProductionConfig) {
  // Simulate the production scenario: 384 MB arena sorting 200K elements.
  // Peak RSS must stay well under 500 MB.
  std::mt19937_64 rng(55);
  std::vector<Element> data(200000);
  for (auto& x : data) x = static_cast<Element>(rng());

  std::string in_path = WriteInput(data);
  ExternalSort::Config cfg;
  cfg.input_path = in_path;
  cfg.output_path = test_dir_ + "/output.txt";
  cfg.temp_dir = test_dir_ + "/runs";
  cfg.arena_bytes = kDefaultArenaBytes;  // 384 MB — the production value

  ExternalSort sorter(std::move(cfg));
  SortStats stats = sorter.Run();

  const size_t cgroup_limit_mb = 500;

  fprintf(stderr,
          "  [500 MB check] peak_rss=%zu MB, anon=%zu MB, file=%zu MB, limit=%zu MB\n",
          stats.peak_rss_kb / 1024, stats.peak_anon_kb / 1024,
          stats.peak_file_kb / 1024, cgroup_limit_mb);

  // peak_rss_kb is VmHWM (kernel's high-water mark for the process lifetime).
  // In a shared test binary, it includes other tests too.  So we also check
  // anon + file from our own sampling, which is scoped to this sort.
  size_t our_peak_mb = stats.peak_anon_kb / 1024 + stats.peak_file_kb / 1024;
  EXPECT_LT(our_peak_mb, cgroup_limit_mb)
      << "FAILED: Peak RSS (" << our_peak_mb
      << " MB) would exceed the 500 MB cgroup limit.\n"
      << "  anon=" << stats.peak_anon_kb / 1024 << " MB"
      << "  file=" << stats.peak_file_kb / 1024 << " MB";
}

// ---------------------------------------------------------------------------
// 4. GetMemInfo sanity test
// ---------------------------------------------------------------------------

TEST(MemMonitorSanityTest, CanReadProcSelfStatus) {
  MemInfo m = GetMemInfo();
  EXPECT_GT(m.vm_rss_kb, 0u) << "VmRSS should be > 0 for any running process";
  EXPECT_GT(m.vm_hwm_kb, 0u) << "VmHWM should be > 0";
  EXPECT_GE(m.vm_hwm_kb, m.vm_rss_kb) << "VmHWM must be >= VmRSS (it's the peak)";
  EXPECT_GT(m.rss_anon_kb, 0u) << "Process should have some anonymous pages (stack, heap)";
}

}  // namespace
}  // namespace atlas
