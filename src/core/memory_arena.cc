#include "memory_arena.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

namespace atlas {

MemoryArena::MemoryArena(size_t size_bytes) : size_(size_bytes) {
  base_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base_ == MAP_FAILED) {
    fprintf(stderr, "mmap(%zu bytes) failed: %s\n", size_, strerror(errno));
    abort();
  }
  // Hint: use transparent huge pages for better TLB behavior during radix sort
  madvise(base_, size_, MADV_HUGEPAGE);
}

MemoryArena::~MemoryArena() {
  if (base_ && base_ != MAP_FAILED) {
    munmap(base_, size_);
  }
}

}  // namespace atlas
