#include "radix_sort.h"
#include <cstring>
#include <vector>

namespace atlas {

void RadixSort(Record* data, Record* scratch, size_t count) {
  if (count < 2) return;

  Record* src = data;
  Record* dst = scratch;

  // Prompt 2: Radix sorting 128-byte Records by 64-bit uint timestamp.
  for (int shift = 0; shift < 64; shift += 8) {
    size_t counts[256] = {0};
    for (size_t i = 0; i < count; ++i) {
      uint8_t byte = (src[i].timestamp >> shift) & 0xFF;
      counts[byte]++;
    }

    size_t offsets[256];
    offsets[0] = 0;
    for (int i = 1; i < 256; ++i) {
      offsets[i] = offsets[i - 1] + counts[i - 1];
    }

    for (size_t i = 0; i < count; ++i) {
      uint8_t byte = (src[i].timestamp >> shift) & 0xFF;
      dst[offsets[byte]++] = src[i];
    }

    // Swap src and dst for the next pass
    std::swap(src, dst);
  }

  // If we ended up in scratch, copy back to data
  if (src != data) {
    std::memcpy(data, scratch, count * sizeof(Record));
  }
}

}  // namespace atlas
