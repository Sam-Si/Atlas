#include "radix_sort.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace atlas {
namespace {

// Flip the sign bit so that the unsigned byte-by-byte ordering
// matches the signed integer ordering:
//   INT64_MIN → 0x0000...0000
//   -1        → 0x7FFF...FFFF
//   0         → 0x8000...0000
//   INT64_MAX → 0xFFFF...FFFF
inline uint64_t ToSortable(int64_t v) {
  return static_cast<uint64_t>(v) ^ (1ULL << 63);
}

inline int64_t FromSortable(uint64_t v) {
  return static_cast<int64_t>(v ^ (1ULL << 63));
}

}  // namespace

void RadixSort(Element* data, Element* scratch, size_t count) {
  if (count <= 1) return;

  auto* src = reinterpret_cast<uint64_t*>(data);
  auto* dst = reinterpret_cast<uint64_t*>(scratch);

  // Convert to sortable unsigned representation
  for (size_t i = 0; i < count; ++i) {
    src[i] = ToSortable(static_cast<int64_t>(src[i]));
  }

  // 8 passes — one per byte, LSB first
  for (int pass = 0; pass < 8; ++pass) {
    const int shift = pass * 8;

    // 1. Count occurrences of each byte value
    size_t counts[256] = {};
    for (size_t i = 0; i < count; ++i) {
      ++counts[(src[i] >> shift) & 0xFF];
    }

    // 2. Prefix-sum → destination offsets
    size_t offsets[256];
    offsets[0] = 0;
    for (int b = 1; b < 256; ++b) {
      offsets[b] = offsets[b - 1] + counts[b - 1];
    }

    // 3. Scatter into dst in stable order
    for (size_t i = 0; i < count; ++i) {
      const uint8_t key = (src[i] >> shift) & 0xFF;
      dst[offsets[key]++] = src[i];
    }

    // After this pass, sorted-so-far data lives in dst.
    // Swap pointers so src always points to "current" data.
    std::swap(src, dst);
  }

  // After 8 passes (even number of swaps), src == data again.
  // Convert back from sortable-unsigned to signed int64.
  for (size_t i = 0; i < count; ++i) {
    data[i] = FromSortable(reinterpret_cast<uint64_t*>(data)[i]);
  }
}

}  // namespace atlas
