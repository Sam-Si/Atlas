#pragma once
#include <cstddef>
#include <cstdio>
#include <sys/resource.h>
#include <sys/time.h>

namespace atlas {

struct MemInfo {
  size_t vm_hwm_kb  = 0;  // VmHWM:  peak RSS ever (kernel high-water mark)
  size_t vm_rss_kb  = 0;  // VmRSS:  current RSS (anon + file + shmem)
  size_t rss_anon_kb = 0; // RssAnon: anonymous pages (mmap arena, heap, stack)
  size_t rss_file_kb = 0; // RssFile: file-backed pages (page cache)
  size_t rusage_maxrss_kb = 0; // ru_maxrss from getrusage
};

/// Snapshot current memory counters from /proc/self/status and getrusage.
inline MemInfo GetMemInfo() {
  MemInfo m;
  
  // 1. /proc/self/status (Linux-specific detailed breakdown)
  FILE* f = std::fopen("/proc/self/status", "r");
  if (f) {
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
      if (std::sscanf(line, "VmHWM: %zu kB", &m.vm_hwm_kb)  == 1) continue;
      if (std::sscanf(line, "VmRSS: %zu kB", &m.vm_rss_kb)  == 1) continue;
      if (std::sscanf(line, "RssAnon: %zu kB", &m.rss_anon_kb) == 1) continue;
      if (std::sscanf(line, "RssFile: %zu kB", &m.rss_file_kb) == 1) continue;
    }
    std::fclose(f);
  }

  // 2. getrusage (POSIX standard peak RSS)
  // Prompt 2: Monitoring peak RAM using getrusage.
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    m.rusage_maxrss_kb = static_cast<size_t>(usage.ru_maxrss);
    // Note: on Linux, ru_maxrss is in kilobytes. On macOS, it's in bytes.
    // Our Docker container is Linux, so it's KB.
  }
  
  return m;
}

}  // namespace atlas
