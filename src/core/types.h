#pragma once
#include <cstddef>
#include <cstdint>

namespace atlas {

/// Prompt 1: The Binary Schema
/// Each record is strictly 128 bytes.
struct Record {
  uint64_t timestamp;   // 8 bytes (The sort key)
  uint32_t feature_id;  // 4 bytes
  uint8_t payload[116]; // 116 bytes
};

static_assert(sizeof(Record) == 128, "Record size must be exactly 128 bytes");

/// Prompt 5: labels schema
struct Label {
  uint64_t timestamp;
  uint32_t label_value;
};

struct JoinedRecord {
  uint64_t timestamp;
  uint32_t feature_id;
  uint32_t label_value;
};

// Default arena for the spill phase.
// 256MB is a safer choice for a 500MB container than 384MB, 
// to leave headroom for OS overhead, page cache, stack, and heap-allocated vectors.
constexpr size_t kDefaultArenaBytes = 256ULL * 1024 * 1024;

}  // namespace atlas
