#include "external_sort.h"
#include "io.h"
#include "mem_monitor.h"
#include "radix_sort.h"
#include "types.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <queue>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace atlas {

// ── helpers ────────────────────────────────────────────────────────────────

namespace {

void MkdirP(const std::string& path) {
  std::string cur;
  for (char c : path) {
    cur += c;
    if (c == '/') mkdir(cur.c_str(), 0755);
  }
  mkdir(cur.c_str(), 0755);
}

using Clock = std::chrono::high_resolution_clock;

double ElapsedSecs(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

}  // namespace

// ── constructor ────────────────────────────────────────────────────────────

ExternalSort::ExternalSort(Config config)
    : config_(std::move(config)),
      arena_(config_.arena_bytes) {
  MkdirP(config_.temp_dir);
}

// ── Run (orchestrator) ─────────────────────────────────────────────────────

SortStats ExternalSort::Run() {
  SortStats stats;
  auto t0 = Clock::now();

  // Helper: sample /proc/self/status and update high-water marks.
  auto track = [&](const char* label) {
    MemInfo m = GetMemInfo();
    if (m.rss_anon_kb > stats.peak_anon_kb) stats.peak_anon_kb = m.rss_anon_kb;
    if (m.rss_file_kb > stats.peak_file_kb) stats.peak_file_kb = m.rss_file_kb;
    fprintf(stderr, "  [mem@%-12s] RSS %5zu MB  (anon %zu MB + file %zu MB)\n",
            label,
            m.vm_rss_kb / 1024,
            m.rss_anon_kb / 1024,
            m.rss_file_kb / 1024);
  };

  track("start");

  // Phase 1
  auto t1 = Clock::now();
  stats.total_elements = GenerateRuns();
  stats.phase1_secs = ElapsedSecs(t1);
  stats.num_runs = run_paths_.size();
  track("phase1_done");
  fprintf(stderr, "Phase 1 done: %zu runs, %zu elements (%.2fs)\n",
          stats.num_runs, stats.total_elements, stats.phase1_secs);

  // Phase 2
  auto t2 = Clock::now();
  MergeRuns();
  stats.phase2_secs = ElapsedSecs(t2);
  track("phase2_done");
  fprintf(stderr, "Phase 2 done: merged to output (%.2fs)\n",
          stats.phase2_secs);

  Cleanup();
  stats.total_secs = ElapsedSecs(t0);

  // VmHWM is the kernel's authoritative peak-RSS over the process lifetime.
  stats.peak_rss_kb = GetMemInfo().vm_hwm_kb;

  return stats;
}

// ── Phase 1: Generate Sorted Runs ─────────────────────────────────────────

size_t ExternalSort::GenerateRuns() {
  // Split arena 50/50: sort buffer | scratch buffer
  const size_t half = arena_.Size() / 2;
  Element* sort_buf = static_cast<Element*>(arena_.Base());
  const size_t sort_cap = half / sizeof(Element);
  Element* scratch = sort_buf + sort_cap;

  TextReader reader(config_.input_path);
  size_t total = 0;
  int run_id = 0;

  while (!reader.Done()) {
    size_t count = reader.ReadBatch(sort_buf, sort_cap);
    if (count == 0) break;

    RadixSort(sort_buf, scratch, count);

    char path[512];
    snprintf(path, sizeof(path), "%s/run_%04d.bin",
             config_.temp_dir.c_str(), run_id++);

    BinaryWriter writer(path);
    writer.Write(sort_buf, count);
    run_paths_.emplace_back(path);

    total += count;
    fprintf(stderr, "  run %d: %zu elements\n", run_id - 1, count);
  }

  total_elements_ = total;
  return total;
}

// ── Phase 2: K-Way Merge ──────────────────────────────────────────────────

void ExternalSort::MergeRuns() {
  if (run_paths_.empty()) {
    // Empty input → empty output
    FILE* f = fopen(config_.output_path.c_str(), "w");
    if (f) fclose(f);
    return;
  }

  const size_t K = run_paths_.size();
  const size_t num_parts = K + 1;  // K input bufs + 1 output buf
  const size_t part_elems = arena_.Size() / num_parts / sizeof(Element);

  // Sanity check: each buffer should have at least a few KB
  if (part_elems < 128) {
    fprintf(stderr, "FATAL: too many runs (%zu) for arena size\n", K);
    abort();
  }

  // Create one BinaryReader per run, each backed by a slice of the arena
  std::vector<std::unique_ptr<BinaryReader>> readers;
  readers.reserve(K);
  for (size_t i = 0; i < K; ++i) {
    Element* buf = static_cast<Element*>(arena_.Base()) + i * part_elems;
    readers.push_back(
        std::make_unique<BinaryReader>(run_paths_[i], buf, part_elems));
  }

  // Output buffer (reinterpreted as char* for text formatting)
  char* out_buf = reinterpret_cast<char*>(
      static_cast<Element*>(arena_.Base()) + K * part_elems);
  const size_t out_cap = part_elems * sizeof(Element);
  size_t out_pos = 0;

  // Open output file
  int out_fd = open(config_.output_path.c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out_fd < 0) {
    fprintf(stderr, "Cannot open output '%s': %s\n",
            config_.output_path.c_str(), strerror(errno));
    abort();
  }

  // Flush lambda
  auto flush = [&]() {
    const char* p = out_buf;
    size_t rem = out_pos;
    while (rem > 0) {
      ssize_t w = ::write(out_fd, p, rem);
      if (w < 0) { perror("write output"); abort(); }
      p += w;
      rem -= static_cast<size_t>(w);
    }
    posix_fadvise(out_fd, 0, 0, POSIX_FADV_DONTNEED);
    out_pos = 0;
  };

  // Min-heap entry
  struct Entry {
    Element value;
    int idx;
    bool operator>(const Entry& o) const { return value > o.value; }
  };

  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

  // Seed the heap with the first element from each run
  for (size_t i = 0; i < K; ++i) {
    if (readers[i]->HasNext()) {
      heap.push({readers[i]->Peek(), static_cast<int>(i)});
      readers[i]->Advance();
    }
  }

  // Merge loop
  while (!heap.empty()) {
    const auto top = heap.top();
    heap.pop();

    // Ensure ≥ 32 bytes free before formatting
    if (out_cap - out_pos < 32) flush();

    int n = snprintf(out_buf + out_pos, 32,
                     "%" PRId64 "\n", static_cast<int64_t>(top.value));
    out_pos += static_cast<size_t>(n);

    const int ri = top.idx;
    if (readers[ri]->HasNext()) {
      heap.push({readers[ri]->Peek(), ri});
      readers[ri]->Advance();
    }
  }

  if (out_pos > 0) flush();
  close(out_fd);
}

// ── Cleanup ───────────────────────────────────────────────────────────────

void ExternalSort::Cleanup() {
  for (const auto& p : run_paths_) {
    ::remove(p.c_str());
  }
  // Try to remove temp dir (only succeeds if empty)
  ::rmdir(config_.temp_dir.c_str());
}

}  // namespace atlas
