#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// mem_monitor.h — Read process memory stats from /proc/self/status.
//
// Provides VmHWM (peak RSS high-water mark tracked by the kernel),
// current VmRSS, and its anon/file breakdown.  All values in KB.
// Returns zeroed struct on non-Linux systems.
// ═══════════════════════════════════════════════════════════════════════════
#include <cstddef>
#include <cstdio>

namespace atlas {

struct MemInfo {
  size_t vm_hwm_kb  = 0;  // VmHWM:  peak RSS ever (kernel high-water mark)
  size_t vm_rss_kb  = 0;  // VmRSS:  current RSS (anon + file + shmem)
  size_t rss_anon_kb = 0; // RssAnon: anonymous pages (mmap arena, heap, stack)
  size_t rss_file_kb = 0; // RssFile: file-backed pages (page cache)
};

/// Snapshot current memory counters from /proc/self/status.
inline MemInfo GetMemInfo() {
  MemInfo m;
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return m;
  char line[256];
  while (std::fgets(line, sizeof(line), f)) {
    // Each line looks like:  "VmHWM:    123456 kB\n"
    if (std::sscanf(line, "VmHWM: %zu kB", &m.vm_hwm_kb)  == 1) continue;
    if (std::sscanf(line, "VmRSS: %zu kB", &m.vm_rss_kb)  == 1) continue;
    if (std::sscanf(line, "RssAnon: %zu kB", &m.rss_anon_kb) == 1) continue;
    if (std::sscanf(line, "RssFile: %zu kB", &m.rss_file_kb) == 1) continue;
  }
  std::fclose(f);
  return m;
}

}  // namespace atlas
