#pragma once
#include <cstddef>

namespace atlas {

/// A fixed-size memory arena backed by mmap.
/// Allocated once, reused for both sort and merge phases.
/// No heap allocations — the arena IS the heap.
class MemoryArena {
 public:
  explicit MemoryArena(size_t size_bytes);
  ~MemoryArena();

  // Non-copyable, non-movable.
  MemoryArena(const MemoryArena&) = delete;
  MemoryArena& operator=(const MemoryArena&) = delete;

  void* Base() const { return base_; }
  size_t Size() const { return size_; }

 private:
  void* base_ = nullptr;
  size_t size_ = 0;
};

}  // namespace atlas
