#include "joiner.h"
#include "io.h"
#include <cinttypes>
#include <cstdio>
#include <vector>

namespace atlas {

StreamingJoiner::StreamingJoiner(const std::string& records_path, const std::string& labels_path)
    : records_path_(records_path), labels_path_(labels_path) {}

void StreamingJoiner::Run() {
  // 10MB RAM limit (Prompt 5). 
  // Record is 128 bytes. Label is 12 bytes.
  // 1MB buffer for each is plenty (8192 records + 87,381 labels).
  const size_t record_buf_elems = (1 * 1024 * 1024) / sizeof(Record);
  const size_t label_buf_elems = (1 * 1024 * 1024) / sizeof(Label);

  std::vector<Record> r_buf(record_buf_elems);
  std::vector<Label> l_buf(label_buf_elems);

  BinaryReader r_reader(records_path_, r_buf.data(), record_buf_elems);
  LabelReader l_reader(labels_path_, l_buf.data(), label_buf_elems);

  size_t joined_count = 0;
  
  fprintf(stderr, "Starting Streaming Join...\n");

  // Prompt 5: Two-pointer streaming join logic
  while (r_reader.HasNext() && l_reader.HasNext()) {
    const auto& rec = r_reader.Peek();
    const auto& lab = l_reader.Peek();

    if (rec.timestamp == lab.timestamp) {
      // Emit joined result (feature_id from record, label_value from label)
      // For performance, we print every 10,000th or just count in this demo
      if (joined_count % 100000 == 0) {
        printf("[JOIN] TS: %" PRIu64 " | Feature: %u | Label: %u\n",
               rec.timestamp, rec.feature_id, lab.label_value);
      }
      joined_count++;
      
      // Advance both (assuming unique timestamps for simplicity, 
      // or handle duplicates by advancing based on logic)
      r_reader.Advance();
      l_reader.Advance();
    } else if (rec.timestamp < lab.timestamp) {
      r_reader.Advance();
    } else {
      l_reader.Advance();
    }
  }

  fprintf(stderr, "Join Complete. Total joined records: %zu\n", joined_count);
}

}  // namespace atlas
