#pragma once
#include "types.h"
#include <string>

namespace atlas {

/// Prompt 5: Massive Sort-Merge Join
/// Joins two sorted binary files (records and labels) based on timestamp.
/// Uses < 10MB of RAM by streaming both files with small buffers.
class StreamingJoiner {
 public:
  StreamingJoiner(const std::string& records_path, const std::string& labels_path);
  
  /// Runs the join and prints joined results to stdout.
  /// Schema: timestamp, feature_id, label_value
  void Run();

 private:
  std::string records_path_;
  std::string labels_path_;
};

}  // namespace atlas
