#pragma once
#include "types.h"

namespace atlas {

/// Prompt 1: The Binary Schema 
/// Custom comparator for Record objects sorted by 64-bit uint timestamp.
struct RecordComparator {
  bool operator()(const Record& a, const Record& b) const {
    return a.timestamp < b.timestamp;
  }
};

/// For std::priority_queue (Min-Heap needs > for min-ordering)
struct RecordMinComparator {
  bool operator()(const Record& a, const Record& b) const {
    return a.timestamp > b.timestamp;
  }
};

}  // namespace atlas
