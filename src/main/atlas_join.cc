#include "src/core/joiner.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static void Usage(const char* prog) {
  fprintf(stderr,
    "Atlas Streaming Joiner — Join sorted records and labels by timestamp.\n\n"
    "Usage: %s --records=FILE --labels=FILE\n\n"
    "Options:\n"
    "  --records=FILE     Sorted binary records (128-byte)\n"
    "  --labels=FILE      Sorted binary labels (12-byte)\n",
    prog);
  exit(1);
}

int main(int argc, char* argv[]) {
  std::string records_path;
  std::string labels_path;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--records=", 0) == 0)      records_path = a.substr(10);
    else if (a.rfind("--labels=", 0) == 0)   labels_path = a.substr(9);
    else { fprintf(stderr, "Unknown flag: %s\n", argv[i]); Usage(argv[0]); }
  }

  if (records_path.empty() || labels_path.empty()) Usage(argv[0]);

  fprintf(stderr,
    "╔══════════════════════════════════════╗\n"
    "║         Atlas Streaming Join         ║\n"
    "╚══════════════════════════════════════╝\n"
    "  Records:  %s\n"
    "  Labels:   %s\n\n",
    records_path.c_str(), labels_path.c_str());

  atlas::StreamingJoiner joiner(records_path, labels_path);
  joiner.Run();

  return 0;
}
