#include "src/core/external_sort.h"
#include "src/core/types.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void Usage(const char* prog) {
  fprintf(stderr,
    "Atlas External Sort — Sort 128-byte binary records within a fixed RAM budget.\n\n"
    "Usage: %s --input=FILE --output=FILE [options]\n\n"
    "Options:\n"
    "  --input=FILE       Input binary file (128-byte records)\n"
    "  --output=FILE      Output binary file (sorted)\n"
    "  --temp-dir=DIR     Directory for temporary spill files [/tmp/atlas_sort]\n"
    "  --arena=BYTES      Arena size in bytes [%zu]\n",
    prog, atlas::kDefaultArenaBytes);
  exit(1);
}

int main(int argc, char* argv[]) {
  atlas::ExternalSort::Config cfg;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--input=", 0) == 0)        cfg.input_path = a.substr(8);
    else if (a.rfind("--output=", 0) == 0)   cfg.output_path = a.substr(9);
    else if (a.rfind("--temp-dir=", 0) == 0) cfg.temp_dir = a.substr(11);
    else if (a.rfind("--arena=", 0) == 0)    cfg.arena_bytes = std::stoull(a.substr(8));
    else { fprintf(stderr, "Unknown flag: %s\n", argv[i]); Usage(argv[0]); }
  }

  if (cfg.input_path.empty() || cfg.output_path.empty()) Usage(argv[0]);

  fprintf(stderr,
    "╔══════════════════════════════════════╗\n"
    "║         Atlas External Sort          ║\n"
    "╚══════════════════════════════════════╝\n"
    "  Input:    %s\n"
    "  Output:   %s\n"
    "  Temp dir: %s\n"
    "  Arena:    %.1f MB\n\n",
    cfg.input_path.c_str(), cfg.output_path.c_str(),
    cfg.temp_dir.c_str(), static_cast<double>(cfg.arena_bytes) / (1024.0 * 1024.0));

  atlas::ExternalSort sorter(cfg);
  atlas::SortStats stats = sorter.Run();

  fprintf(stderr,
    "\n═══ Results ═══════════════════════════\n"
    "  Records sorted:  %zu\n"
    "  Spill files:     %zu\n"
    "  Phase 1 (spill): %.3f s\n"
    "  Phase 2 (merge): %.3f s\n"
    "  ─────────────────────────\n"
    "  TOTAL TIME:      %.3f s\n"
    "\n"
    "── Memory ────────────────────────────\n"
    "  Peak RSS (getrusage): %zu MB\n"
    "  Arena configured:     %.0f MB\n"
    "═══════════════════════════════════════\n",
    stats.total_elements, stats.num_runs,
    stats.phase1_secs, stats.phase2_secs, stats.total_secs,
    stats.peak_rss_kb / 1024,
    static_cast<double>(cfg.arena_bytes) / (1024.0 * 1024.0));

  return 0;
}
