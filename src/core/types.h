#pragma once
#include <cstddef>
#include <cstdint>

namespace atlas {

using Element = int64_t;

// Default arena: 384 MB (leaves ~116 MB for OS/stack inside a 500 MB cgroup)
constexpr size_t kDefaultArenaBytes = 384ULL * 1024 * 1024;

}  // namespace atlas
