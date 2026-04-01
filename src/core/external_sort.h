#pragma once
#include "mem_monitor.h"
#include "memory_arena.h"
#include "types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace atlas {

struct SortStats {
  double phase1_secs = 0;
  double phase2_secs = 0;
  double total_secs = 0;
  size_t total_elements = 0;
  size_t num_runs = 0;

  // ── Memory tracking ──────────────────────────────────────────────────
  // Populated by ExternalSort::Run() via /proc/self/status.
  size_t peak_rss_kb    = 0;  // VmHWM — kernel-tracked peak RSS
  size_t peak_anon_kb   = 0;  // Highest RssAnon seen (arena + heap + stack)
  size_t peak_file_kb   = 0;  // Highest RssFile seen (page cache)
};

class ExternalSort {
 public:
  struct Config {
    std::string input_path;
    std::string output_path;
    std::string temp_dir = "/tmp/atlas_sort";
    size_t arena_bytes = kDefaultArenaBytes;  // 384 MB
  };

  explicit ExternalSort(Config config);

  /// Run Phase 1 (chunk+sort) then Phase 2 (merge).  Returns stats.
  SortStats Run();

 private:
  /// Phase 1: read text → sort chunks → write binary run files.
  size_t GenerateRuns();

  /// Phase 2: K-way merge binary runs → write text output.
  void MergeRuns();

  /// Delete temporary run files.
  void Cleanup();

  Config config_;
  MemoryArena arena_;
  std::vector<std::string> run_paths_;
  size_t total_elements_ = 0;
};

}  // namespace atlas
