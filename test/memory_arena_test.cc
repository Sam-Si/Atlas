#include "src/core/memory_arena.h"
#include "src/core/types.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

namespace atlas {
namespace {

TEST(MemoryArenaTest, AllocatesRequestedSize) {
  constexpr size_t kSize = 1024 * 1024;  // 1 MB
  MemoryArena arena(kSize);
  EXPECT_NE(arena.Base(), nullptr);
  EXPECT_EQ(arena.Size(), kSize);
}

TEST(MemoryArenaTest, ReadWriteInts) {
  constexpr size_t kSize = 4096;
  MemoryArena arena(kSize);

  auto* data = static_cast<Element*>(arena.Base());
  size_t count = kSize / sizeof(Element);

  for (size_t i = 0; i < count; ++i) {
    data[i] = static_cast<Element>(i) * 137;
  }
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(data[i], static_cast<Element>(i) * 137);
  }
}

TEST(MemoryArenaTest, LargeAllocationTouchAllPages) {
  constexpr size_t kSize = 64 * 1024 * 1024;  // 64 MB
  MemoryArena arena(kSize);
  EXPECT_NE(arena.Base(), nullptr);

  // Touch every page (4 KB) to verify the mapping is valid
  auto* p = static_cast<char*>(arena.Base());
  for (size_t off = 0; off < kSize; off += 4096) {
    p[off] = static_cast<char>(off & 0xFF);
  }
  for (size_t off = 0; off < kSize; off += 4096) {
    EXPECT_EQ(p[off], static_cast<char>(off & 0xFF));
  }
}

TEST(MemoryArenaTest, PageAligned) {
  MemoryArena arena(8192);
  // mmap always returns page-aligned addresses
  EXPECT_EQ(reinterpret_cast<uintptr_t>(arena.Base()) % 4096, 0u);
}

}  // namespace
}  // namespace atlas
