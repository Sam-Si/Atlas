#pragma once
#include "types.h"
#include <cstddef>

namespace atlas {

/// LSD Radix Sort for int64_t arrays.
///
/// Sorts `data[0..count)` in ascending order.
/// `scratch` must point to a buffer of at least `count` elements.
/// After return, sorted data is in `data`.
///
/// Handles negative numbers correctly via sign-bit flipping.
/// Time:  O(8·n) — 8 passes, one per byte of int64_t.
/// Space: uses `scratch` (same size as data) — no allocations.
void RadixSort(Element* data, Element* scratch, size_t count);

}  // namespace atlas
