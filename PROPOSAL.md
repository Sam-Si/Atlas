# Atlas — External Sort: 10+ GB of Integers in 500 MB of RAM

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [Why This Is Hard](#2-why-this-is-hard)
3. [The Algorithm: External Merge Sort](#3-the-algorithm-external-merge-sort)
4. [Memory Budget — Every Byte Accounted For](#4-memory-budget--every-byte-accounted-for)
5. [Project Architecture & File Layout](#5-project-architecture--file-layout)
6. [Component Deep-Dive](#6-component-deep-dive)
7. [Performance Optimizations](#7-performance-optimizations)
8. [Docker Configuration — Enforcing the 500 MB Limit](#8-docker-configuration--enforcing-the-500-mb-limit)
9. [Bazel Build System](#9-bazel-build-system)
10. [Testing & Validation Strategy](#10-testing--validation-strategy)
11. [End-to-End Walkthrough](#11-end-to-end-walkthrough)

---

## 1. Problem Statement

```
┌──────────────────────────────────────────────────────┐
│  INPUT:   10+ GB binary file of 64-bit integers      │
│  OUTPUT:  Sorted binary file of 64-bit integers       │
│  LIMIT:   500 MB total RAM (hard Docker cgroup limit) │
│  GOAL:    No OOM. As fast as physically possible.     │
└──────────────────────────────────────────────────────┘
```

- **10 GB of `int64_t`** = **1.34 billion integers** (10 × 1024³ ÷ 8).
- **500 MB of RAM** can hold only **~62.5 million** of them at a time (~5% of the data).
- We **cannot** load the data into memory. We must use an **external sort**.

---

## 2. Why This Is Hard

| Challenge | Why It Matters |
|---|---|
| **Data ≫ RAM** | 10 GB ÷ 500 MB = 20×. We must stream data through RAM in pieces. |
| **OS overhead** | The kernel, page tables, libc, and stack eat ~50–80 MB before our code runs a single line. |
| **Page cache competition** | Linux will happily use your 500 MB for page cache, leaving nothing for your sort buffers → OOM. |
| **I/O is the bottleneck** | We read 10 GB, write 10 GB of chunks, read those back, write 10 GB of output = **30–40 GB of I/O**. Disk speed dominates. |
| **Too many temp files** | If chunk size is too small we get hundreds of runs → merge needs hundreds of open files with tiny buffers → slow random I/O. |

**Bottom line:** We need to control memory down to the byte and maximize sequential I/O throughput.

---

## 3. The Algorithm: External Merge Sort

External merge sort is the textbook solution. It works in two phases:

### Phase 1 — Chunk, Sort, Flush (the "Run Generation" phase)

```
                         500 MB RAM
                    ┌───────────────────┐
  10 GB input ───►  │  Read 384 MB      │ ───► run_00.bin (384 MB, sorted)
  (unsorted)        │  Radix-sort it    │ ───► run_01.bin (384 MB, sorted)
                    │  Write to disk    │ ───► run_02.bin (384 MB, sorted)
                    │  Repeat...        │      ...
                    └───────────────────┘ ───► run_26.bin (remaining, sorted)
```

1. Allocate a **single 384 MB sort buffer** (leaving ~116 MB for the OS, stack, I/O overhead).
2. Read 384 MB of raw integers from the input file into the buffer.
3. **Radix sort** the buffer in-place — **O(n)** time, zero extra memory.
4. Write the sorted buffer to a temporary "run" file on disk.
5. Repeat until the entire input is consumed.

**Result:** For 10 GB input → **~27 sorted run files**, each ~384 MB.

### Phase 2 — K-Way Merge (the "Merge" phase)

```
  run_00.bin ──► ┌───buf 14MB──┐
  run_01.bin ──► │  buf 14MB   │        ┌───────────┐
  run_02.bin ──► │  buf 14MB   │──min──►│  Output    │───► sorted_output.bin
  ...            │  ...        │ heap   │  buf 14MB  │     (10 GB, sorted)
  run_26.bin ──► │  buf 14MB   │        └───────────┘
                 └─────────────┘
                  Total: ~400 MB
```

1. Open all 27 run files simultaneously.
2. Give each file an **input buffer** of ~14 MB (`384 MB ÷ 28 ≈ 13.7 MB`).
3. Allocate one **output buffer** of ~14 MB.
4. Fill each input buffer with the first 14 MB from its run file.
5. Use a **min-heap** (priority queue) of size 27 — one entry per run.
6. Pop the smallest element, write it to the output buffer.
7. When a run's input buffer is drained, refill it from disk.
8. When the output buffer is full, flush it to the final output file.
9. Continue until all runs are exhausted.

**Result:** One fully sorted 10 GB output file.

### Why Only Two Passes?

| Pass | Reads | Writes | Total I/O |
|---|---|---|---|
| Phase 1 (run generation) | 10 GB | 10 GB | 20 GB |
| Phase 2 (merge) | 10 GB | 10 GB | 20 GB |
| **Total** | **20 GB** | **20 GB** | **40 GB** |

Two passes means we read and write the data exactly twice. Any more passes would
linearly multiply I/O. The key insight: with 384 MB chunks and 10 GB of data we
get ~27 runs, which is easily mergeable in a single merge pass (we'd need 500+ runs
before requiring a multi-pass merge, which would be ~200 GB+ of data).

---

## 4. Memory Budget — Every Byte Accounted For

We treat 500 MB as a **hard ceiling**. Here's the allocation:

```
┌─────────────────────────────────────────────────────────┐
│                    500 MB TOTAL                          │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────────────────────┐                           │
│  │  OS / kernel / libc      │  ~50 MB                   │
│  │  (page tables, stack,    │                           │
│  │   thread stacks, mmap)   │                           │
│  └──────────────────────────┘                           │
│                                                         │
│  ┌──────────────────────────┐                           │
│  │  Sort Buffer (Phase 1)   │  384 MB                   │
│  │  = 48 million int64s     │  (reused as merge         │
│  │                          │   buffers in Phase 2)     │
│  └──────────────────────────┘                           │
│                                                         │
│  ┌──────────────────────────┐                           │
│  │  Min-heap (Phase 2)      │  ~1 KB (27 entries)       │
│  └──────────────────────────┘                           │
│                                                         │
│  ┌──────────────────────────┐                           │
│  │  Bookkeeping / metadata  │  ~16 MB                   │
│  │  (file descriptors,      │                           │
│  │   counters, config)      │                           │
│  └──────────────────────────┘                           │
│                                                         │
│  ┌──────────────────────────┐                           │
│  │  Safety margin           │  ~50 MB                   │
│  └──────────────────────────┘                           │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  TOTAL ACCOUNTED:  ~500 MB                              │
└─────────────────────────────────────────────────────────┘
```

### Critical rules to avoid OOM:

1. **One big allocation, done once.** Allocate the 384 MB arena at startup with
   `mmap(MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE)`. Reuse it for both phases.
   No `new`, no `malloc` in the hot path.

2. **No page cache bloat.** Use `O_DIRECT` for all file I/O so the kernel doesn't
   mirror our buffers in its page cache (which counts against our cgroup limit).
   If `O_DIRECT` isn't available, use `posix_fadvise(POSIX_FADV_DONTNEED)` after
   every read/write to evict pages.

3. **No STL containers in the hot path.** `std::vector` can silently double its
   allocation. We use raw pointers into our pre-allocated arena.

4. **mlock the arena.** Prevent the OS from swapping out our working memory.

---

## 5. Project Architecture & File Layout

```
atlas/
├── MODULE.bazel                      # Bazel module (already exists)
├── BUILD                             # Root BUILD (already exists)
├── requirements.txt                  # Python deps (already exists)
├── PROPOSAL.md                       # ← This document
│
├── docker/
│   ├── Dockerfile                    # Builds the entire project inside container
│   └── run.sh                        # Helper: docker run --memory=500m ...
│
├── proto/
│   ├── BUILD                         # Protobuf build rules
│   └── sort_config.proto             # Sort configuration message
│
├── src/
│   ├── core/                         # ★ The sorting engine (C++)
│   │   ├── BUILD
│   │   ├── types.h                   # int64_t aliases, constants
│   │   ├── memory_arena.h            # Fixed 384 MB arena, no heap allocs
│   │   ├── memory_arena.cc
│   │   ├── buffered_io.h             # O_DIRECT aligned buffered I/O
│   │   ├── buffered_io.cc
│   │   ├── radix_sort.h              # LSD radix sort for int64_t
│   │   ├── radix_sort.cc
│   │   ├── merge_engine.h            # K-way merge with min-heap
│   │   ├── merge_engine.cc
│   │   ├── external_sort.h           # Top-level orchestrator (Phase 1 + 2)
│   │   └── external_sort.cc
│   │
│   ├── gen/                          # Data generator tool
│   │   ├── BUILD
│   │   └── data_generator.cc         # Creates random binary int64 files
│   │
│   └── main/                         # CLI entry point
│       ├── BUILD
│       └── atlas_sort.cc             # Parses args, runs external_sort
│
├── test/
│   ├── BUILD
│   ├── memory_arena_test.cc
│   ├── radix_sort_test.cc
│   ├── merge_engine_test.cc
│   ├── external_sort_test.cc         # End-to-end correctness
│   └── sort_benchmark.cc             # google benchmark for throughput
│
└── scripts/
    ├── generate_data.sh              # Generate 1GB / 10GB / 50GB test files
    ├── run_in_docker.sh              # Full pipeline: build, gen, sort, verify
    └── verify_sorted.sh              # Reads output, confirms ascending order
```

---

## 6. Component Deep-Dive

### 6.1 `memory_arena` — The Memory Fortress

```cpp
// Allocate exactly once at startup. Never free until exit.
class MemoryArena {
 public:
  explicit MemoryArena(size_t size_bytes);  // mmap + mlock
  ~MemoryArena();                           // munmap

  // Get a raw pointer into the arena for Phase 1 (sort buffer)
  int64_t* AsSortBuffer(size_t* out_count);

  // Partition the arena into K+1 equal buffers for Phase 2 (merge)
  // Returns K input buffers + 1 output buffer
  struct MergeBuffers {
    std::vector<std::span<int64_t>> input_buffers;  // one per run
    std::span<int64_t> output_buffer;
  };
  MergeBuffers AsMergeBuffers(int num_runs);

 private:
  void* base_;
  size_t size_;
};
```

**Why?** Every byte is pre-allocated. No surprise allocations. No OOM.

### 6.2 `radix_sort` — O(n) Integer Sort

We use **LSD (Least Significant Digit) Radix Sort** with a byte-sized radix (256 buckets):

```
Pass 1: Sort by byte 0 (bits 0–7)     ←  8 passes total for int64_t
Pass 2: Sort by byte 1 (bits 8–15)       (can reduce to 4 passes with
Pass 3: Sort by byte 2 (bits 16–23)       16-bit radix, trading memory
...                                        for fewer passes)
Pass 8: Sort by byte 7 (bits 56–63)
```

- **Time:** O(8n) = O(n) — constant 8 passes over the data.
- **Space:** Needs a scratch buffer the same size as the input.
  We split our 384 MB arena in half: 192 MB input, 192 MB scratch.
  That gives us **24 million integers per chunk** (still very good).

  *Alternative:* Use **in-place MSB radix sort** to avoid the scratch space,
  getting the full 48M integers/chunk. Slightly more complex but no extra memory.

- **Handles negatives:** We flip the sign bit before sorting, flip it back after.
  This makes the unsigned radix sort produce correct signed ordering.

### 6.3 `buffered_io` — Direct I/O With Explicit Buffers

```cpp
class BufferedReader {
 public:
  // buffer must be aligned to 4096 bytes (for O_DIRECT)
  BufferedReader(const std::string& path, int64_t* buffer, size_t buf_count);

  // Returns number of elements read, 0 when EOF
  size_t Refill();

  // Access current buffer contents
  std::span<const int64_t> Current() const;

 private:
  int fd_;            // opened with O_DIRECT
  int64_t* buffer_;
  size_t capacity_;
  size_t valid_;      // how many elements are currently valid
  size_t offset_;     // current read position in buffer
};

class BufferedWriter {
 public:
  BufferedWriter(const std::string& path, int64_t* buffer, size_t buf_count);

  void Write(int64_t value);
  void Flush();  // write remaining buffer contents to disk

 private:
  int fd_;
  int64_t* buffer_;
  size_t capacity_;
  size_t pos_;
};
```

**Why `O_DIRECT`?** Without it, every `read()` creates a **copy** in the kernel page cache.
With a 500 MB cgroup limit, the page cache eats into our 500 MB budget. `O_DIRECT` bypasses
this — data goes straight from disk to our buffer, using only the memory we explicitly allocated.

### 6.4 `merge_engine` — K-Way Merge

```cpp
struct HeapEntry {
  int64_t value;       // current smallest value from this run
  int run_index;       // which run file it came from
  bool operator>(const HeapEntry& o) const { return value > o.value; }
};

class MergeEngine {
 public:
  MergeEngine(std::vector<BufferedReader>& readers,
              BufferedWriter& writer);

  void Run();  // merge all runs into the writer

 private:
  // Min-heap — tiny memory footprint (27 entries × 16 bytes = 432 bytes)
  std::priority_queue<HeapEntry, std::vector<HeapEntry>,
                      std::greater<HeapEntry>> heap_;
};
```

The min-heap is the brain of Phase 2. For 27 runs it's only **432 bytes**. The real memory
goes to the **I/O buffers**, which we carved out of the arena in `AsMergeBuffers()`.

### 6.5 `external_sort` — The Orchestrator

```cpp
class ExternalSort {
 public:
  struct Config {
    std::string input_path;
    std::string output_path;
    std::string temp_dir;           // where run files go (ideally fast SSD)
    size_t memory_limit_bytes;      // 500 MB
    size_t arena_size_bytes;        // 384 MB (derived from memory_limit)
  };

  explicit ExternalSort(Config config);

  void Run();  // Execute Phase 1 + Phase 2

 private:
  void GenerateRuns();     // Phase 1
  void MergeRuns();        // Phase 2
  void CleanupTempFiles(); // Delete run files

  Config config_;
  MemoryArena arena_;
  std::vector<std::string> run_paths_;
};
```

---

## 7. Performance Optimizations

Listed in order of impact:

| # | Optimization | Impact | How |
|---|---|---|---|
| 1 | **Radix sort instead of `std::sort`** | ~3–5× faster for integers | O(n) vs O(n log n). For 48M ints: radix ≈ 0.1s, std::sort ≈ 0.5s |
| 2 | **O_DIRECT file I/O** | Eliminates page cache OOM risk | `open()` with `O_DIRECT` flag, 4KB-aligned buffers |
| 3 | **Large sequential I/O** | Maximize disk throughput | Read/write in 14+ MB chunks, not small pieces |
| 4 | **`-O3 -march=native`** | 20–40% faster sort/merge | Compiler auto-vectorizes loops, uses best ISA |
| 5 | **Single arena, zero allocations** | No malloc overhead, no fragmentation | `mmap` once, reuse for both phases |
| 6 | **`posix_fadvise(DONTNEED)`** | Keeps page cache minimal if O_DIRECT unavailable | After each read/write, tell kernel to drop pages |
| 7 | **Async I/O prefetch** (optional) | Overlap I/O with computation | Background thread fills next buffer while we sort current |
| 8 | **`madvise(MADV_HUGEPAGES)`** | Fewer TLB misses | Radix sort touches memory pseudo-randomly; hugepages help |

### Throughput Estimates (NVMe SSD)

| Phase | Data | Bottleneck | Est. Time |
|---|---|---|---|
| Phase 1: 27× (read 384 MB + sort + write 384 MB) | 20 GB I/O | Disk (~2 GB/s) | ~10s I/O + ~3s sort |
| Phase 2: merge 27 runs → 10 GB output | 20 GB I/O | Disk (~2 GB/s) | ~10s I/O + ~1s heap |
| **Total** | **40 GB I/O** | | **~25–30s** |

On a spinning HDD (~150 MB/s), expect **~5–6 minutes**. This is I/O-bound regardless.

---

## 8. Docker Configuration — Enforcing the 500 MB Limit

### `docker/Dockerfile`

```dockerfile
FROM ubuntu:22.04 AS builder

# Install Bazel + C++ toolchain
RUN apt-get update && apt-get install -y \
    g++-12 \
    curl \
    gnupg \
    && curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > /etc/apt/trusted.gpg.d/bazel.gpg \
    && echo "deb https://storage.googleapis.com/bazel-apt stable jdk1.8" > /etc/apt/sources.list.d/bazel.list \
    && apt-get update && apt-get install -y bazel \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /atlas
COPY . .

# Build everything with maximum optimization
RUN bazel build //src/main:atlas_sort //src/gen:data_generator \
    --compilation_mode=opt \
    -c opt \
    --copt=-O3 \
    --copt=-march=native \
    --copt=-flto \
    --linkopt=-flto

# --- Runtime stage (minimal image) ---
FROM ubuntu:22.04 AS runtime
COPY --from=builder /atlas/bazel-bin/src/main/atlas_sort /usr/local/bin/
COPY --from=builder /atlas/bazel-bin/src/gen/data_generator /usr/local/bin/
```

### `docker/run.sh`

```bash
#!/bin/bash
# The money line: --memory=500m enforces the 500 MB hard limit via cgroups.
# --memory-swap=500m prevents swap from masking leaks.
docker run \
  --memory=500m \
  --memory-swap=500m \
  --rm \
  -v "$(pwd)/data:/data" \
  atlas:latest \
  atlas_sort --input=/data/input.bin --output=/data/sorted.bin --temp-dir=/data/tmp
```

Key points:
- `--memory=500m` → Linux cgroup hard limit. Kernel OOM-kills if exceeded.
- `--memory-swap=500m` → Same as memory limit = **no swap**. This ensures we truly
  stay within 500 MB with no escape hatch.
- Multi-stage build → runtime image is tiny (just the two binaries + libc).
- `-O3 -march=native -flto` → max compiler optimization + link-time optimization.

---

## 9. Bazel Build System

### Root `BUILD`

Empty (as-is). All targets are in subdirectories.

### `src/core/BUILD`

```python
cc_library(
    name = "types",
    hdrs = ["types.h"],
)

cc_library(
    name = "memory_arena",
    srcs = ["memory_arena.cc"],
    hdrs = ["memory_arena.h"],
    deps = [":types", "@com_google_absl//absl/log"],
)

cc_library(
    name = "radix_sort",
    srcs = ["radix_sort.cc"],
    hdrs = ["radix_sort.h"],
    deps = [":types"],
)

cc_library(
    name = "buffered_io",
    srcs = ["buffered_io.cc"],
    hdrs = ["buffered_io.h"],
    deps = [":types", "@com_google_absl//absl/log"],
)

cc_library(
    name = "merge_engine",
    srcs = ["merge_engine.cc"],
    hdrs = ["merge_engine.h"],
    deps = [":buffered_io", ":types"],
)

cc_library(
    name = "external_sort",
    srcs = ["external_sort.cc"],
    hdrs = ["external_sort.h"],
    deps = [":memory_arena", ":radix_sort", ":buffered_io", ":merge_engine"],
    visibility = ["//visibility:public"],
)
```

### `src/main/BUILD`

```python
cc_binary(
    name = "atlas_sort",
    srcs = ["atlas_sort.cc"],
    deps = ["//src/core:external_sort",
            "@com_google_absl//absl/flags:flag",
            "@com_google_absl//absl/flags:parse"],
    copts = ["-O3"],
    linkopts = ["-lpthread"],
)
```

### `test/BUILD`

```python
cc_test(
    name = "radix_sort_test",
    srcs = ["radix_sort_test.cc"],
    deps = ["//src/core:radix_sort", "@googletest//:gtest_main"],
)

cc_test(
    name = "external_sort_test",
    srcs = ["external_sort_test.cc"],
    deps = ["//src/core:external_sort", "@googletest//:gtest_main"],
    size = "large",
)

cc_binary(
    name = "sort_benchmark",
    srcs = ["sort_benchmark.cc"],
    deps = ["//src/core:radix_sort",
            "//src/core:external_sort",
            "@com_github_google_benchmark//:benchmark_main"],
)
```

**Every build and test runs through Bazel. Every run happens in Docker.**

---

## 10. Testing & Validation Strategy

### Level 1: Unit Tests

| Test | What It Validates |
|---|---|
| `memory_arena_test` | Arena allocates exactly N bytes, `AsMergeBuffers` partitions correctly |
| `radix_sort_test` | Sorted output matches `std::sort`. Handles negatives, zeros, duplicates, INT64_MIN/MAX |
| `merge_engine_test` | Merging K pre-sorted sequences produces correct output |

### Level 2: Integration Tests

| Test | What It Validates |
|---|---|
| `external_sort_test` (small) | 1 MB file → sorted correctly end-to-end |
| `external_sort_test` (medium) | 100 MB file → sorted correctly, uses multiple chunks |
| `external_sort_test` (edge cases) | Already sorted, reverse sorted, all same value, single element |

### Level 3: Docker Constraint Tests

```bash
# Generate 10 GB of random integers
docker run --memory=500m atlas:latest data_generator --size=10GB --output=/data/input.bin

# Sort with the hard 500 MB limit
docker run --memory=500m --memory-swap=500m atlas:latest \
  atlas_sort --input=/data/input.bin --output=/data/sorted.bin

# Verify: exit code 0 = correctly sorted
docker run atlas:latest verify_sorted /data/sorted.bin
```

If the sort binary touches byte 500,000,001, the kernel kills it. No mercy.

### Level 4: Benchmarks (Google Benchmark)

```
BM_RadixSort/48M_ints         ~100 ms     (in-memory sort throughput)
BM_ExternalSort/1GB           ~3 s        (end-to-end, NVMe)
BM_ExternalSort/10GB          ~25 s       (end-to-end, NVMe)
BM_MergeEngine/27_runs        ~10 s       (merge phase alone)
```

---

## 11. End-to-End Walkthrough

Here's exactly what happens when you run `./docker/run.sh`:

```
Step 1:  Docker creates a container with a 500 MB memory cgroup.
         │
Step 2:  atlas_sort starts. Parses flags.
         │
Step 3:  MemoryArena allocates 384 MB via mmap().
         │    ┌──────────────────────────────────────────┐
         │    │ mmap(NULL, 384MB, PROT_READ|PROT_WRITE,  │
         │    │       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)  │
         │    └──────────────────────────────────────────┘
         │    Arena is locked in memory with mlock().
         │
Step 4:  ═══════════ PHASE 1: RUN GENERATION ═══════════
         │
         │    Opens input.bin with O_DIRECT.
         │
         │    Loop (27 iterations for 10 GB):
         │    ├── Read 384 MB into arena           (~0.2s on NVMe)
         │    ├── Radix sort the arena in-place     (~0.1s)
         │    ├── Write arena to run_XX.bin          (~0.2s on NVMe)
         │    └── Log: "Run 17/27 generated"
         │
         │    Total Phase 1: ~13s
         │
Step 5:  ═══════════ PHASE 2: K-WAY MERGE ═════════════
         │
         │    Partition arena into 28 buffers (27 input + 1 output).
         │    Open all 27 run files + output file with O_DIRECT.
         │
         │    Fill each input buffer (Refill from its run file).
         │    Build min-heap from the first element of each buffer.
         │
         │    Loop (1.34 billion iterations):
         │    ├── Pop min from heap → write to output buffer
         │    ├── If output buffer full → Flush to disk
         │    ├── Advance that run's buffer cursor
         │    ├── If run buffer drained → Refill from disk
         │    └── Push next element from that run into heap
         │
         │    Final Flush.
         │    Total Phase 2: ~15s
         │
Step 6:  Delete run_00.bin through run_26.bin.
         │
Step 7:  Print summary:
         │    "Sorted 1,342,177,280 integers (10.00 GB) in 28.3s"
         │    "Peak RSS: 437 MB / 500 MB limit"
         │
Step 8:  Exit 0. Docker destroys the container.
```

---

## Summary

| Aspect | Decision | Why |
|---|---|---|
| **Language** | C++ | Byte-level memory control, zero overhead, SIMD-ready |
| **Algorithm** | External Merge Sort (2-pass) | Minimum I/O for data ≫ RAM |
| **In-memory sort** | LSD Radix Sort | O(n) for integers, ~5× faster than comparison sort |
| **Memory** | Single `mmap` arena, reused | One allocation, no fragmentation, no surprises |
| **File I/O** | `O_DIRECT` with aligned buffers | Prevents page cache from stealing our 500 MB |
| **Build** | Bazel with `-O3 -march=native -flto` | Reproducible, fast, optimized binaries |
| **Runtime** | Docker `--memory=500m --memory-swap=500m` | Hard kernel-enforced limit, no swap escape |
| **Testing** | GoogleTest + Google Benchmark | Correctness at every layer, throughput measurement |

**The plan is simple: read in chunks → radix sort → write sorted runs → merge all runs → done. Two passes over the data, every byte of RAM accounted for, and Docker enforces the contract.**
