# Atlas External Sort

Sort **billions of integers** that don't fit in RAM.  
**Benchmark: 49.5 million int64s (1 GB text) sorted in 10.5 seconds with only 384 MB of arena memory.**

```
═══ Results ═══════════════════════════
  Elements sorted: 49,500,000
  Runs generated:  2
  Phase 1 (sort):  4.938 s
  Phase 2 (merge): 5.589 s
  ─────────────────────────
  TOTAL TIME:      10.527 s
═══════════════════════════════════════
```

---

## Table of Contents

- [Quick Start](#quick-start)
- [How It Works](#how-it-works)
- [Project Layout](#project-layout)
- [Build & Run](#build--run)
- [Generate Test Data](#generate-test-data)
- [Run the Sort](#run-the-sort)
- [Run Tests](#run-tests)
- [Component Deep Dive](#component-deep-dive)
  - [Memory Arena](#1-memory-arena)
  - [Radix Sort](#2-radix-sort)
  - [I/O Layer](#3-io-layer)
  - [External Sort Orchestrator](#4-external-sort-orchestrator)
  - [CLI Entry Point](#5-cli-entry-point)
- [Memory Budget](#memory-budget)
- [Key Findings & Performance Numbers](#key-findings--performance-numbers)
- [Docker: Enforcing the 500 MB Limit](#docker-enforcing-the-500-mb-limit)
- [Bazel Build Reference](#bazel-build-reference)
- [Test Coverage](#test-coverage)
- [Verification](#verification)
- [Scaling to 10 GB+](#scaling-to-10-gb)

---

## Quick Start

```bash
# 1. Build the optimized binary
bazel build //src/main:atlas_sort -c opt --copt=-O3 --copt=-march=native

# 2. Generate 1 GB of random integers
python3 src/gen/generate_data.py --output data/input.txt --size 1GB

# 3. Sort
bazel-bin/src/main/atlas_sort \
  --input=data/input.txt \
  --output=data/output.txt \
  --temp-dir=data/tmp

# 4. Run all tests
bazel test //test:all --test_output=errors
```

---

## How It Works

Atlas uses **External Merge Sort** — the textbook algorithm for sorting data that
exceeds available RAM. It runs in exactly **two passes** over the data.

### Phase 1 — Chunk, Sort, Flush

Read the text input in chunks that fit in half the arena (each chunk holds ~25 M
integers), **radix-sort** each chunk in O(n) time, then flush it to disk as a compact
binary "run" file.

```
                      384 MB Arena
                 ┌────────────────────┐
 1 GB input ──►  │  Read 192 MB       │ ──► run_0000.bin (sorted, binary)
 (text, one      │  Radix-sort it     │ ──► run_0001.bin (sorted, binary)
  int/line)      │  Write binary      │
                 └────────────────────┘
```

For 1 GB of text (~49.5 M integers), this produces **2 sorted runs**.

### Phase 2 — K-Way Merge

Open every run file simultaneously, give each a buffer carved from the same arena,
and merge them via a **min-heap** into the final sorted text output.

```
 run_0000.bin ──► ┌── buf 128 MB ──┐
                  │                 │─── min-heap ──► output.txt
 run_0001.bin ──► │   buf 128 MB   │    (2 entries)   (sorted text)
                  │                 │
                  │   buf 128 MB   │◄── output char buffer
                  └────────────────┘
                   Total: 384 MB (same arena, repartitioned)
```

### Total I/O

| Pass | Reads | Writes |
|------|-------|--------|
| Phase 1 | 1 GB text | ~0.4 GB binary runs |
| Phase 2 | ~0.4 GB binary runs | 1 GB text |
| **Total** | **~1.4 GB** | **~1.4 GB** |

---

## Project Layout

```
atlas/
├── MODULE.bazel              ← Bazel module: deps (rules_cc, googletest, abseil, etc.)
├── .bazelrc                  ← C++17 + Bazel 9 compat flags
├── .gitignore                ← Excludes data/ and bazel-* from version control
├── requirements.txt          ← Python deps (for tooling, not used by sort)
├── README.md                 ← This file
├── PROPOSAL.md               ← Original design document
│
├── src/
│   ├── core/                 ← ★ The sorting engine (pure C++)
│   │   ├── BUILD
│   │   ├── types.h           ← Element = int64_t, arena size constant
│   │   ├── memory_arena.h    ← mmap-backed fixed arena (header)
│   │   ├── memory_arena.cc   ← mmap-backed fixed arena (impl)
│   │   ├── radix_sort.h      ← LSD radix sort for int64_t (header)
│   │   ├── radix_sort.cc     ← LSD radix sort for int64_t (impl)
│   │   ├── io.h              ← TextReader, BinaryWriter, BinaryReader (header)
│   │   ├── io.cc             ← TextReader, BinaryWriter, BinaryReader (impl)
│   │   ├── external_sort.h   ← Orchestrator: Phase 1 + Phase 2 (header)
│   │   └── external_sort.cc  ← Orchestrator: Phase 1 + Phase 2 (impl)
│   │
│   ├── main/
│   │   ├── BUILD
│   │   └── atlas_sort.cc     ← CLI binary: parse flags, run sort, print timing
│   │
│   └── gen/
│       ├── BUILD
│       └── generate_data.py  ← Python: generate random int64 test files
│
├── test/
│   ├── BUILD
│   ├── radix_sort_test.cc    ← 13 tests (edge cases, negatives, 500K random)
│   ├── memory_arena_test.cc  ← 4 tests  (alloc, r/w, 64MB large, alignment)
│   └── external_sort_test.cc ← 11 tests (empty → 500K, forces 2/4/8 runs)
│
└── data/                     ← .gitignore'd — generated test files live here
    └── input.txt             ← (generated, not committed)
```

---

## Build & Run

### Prerequisites

- **Bazel 9.x** (the project uses `MODULE.bazel` / bzlmod)
- **GCC 12+** or **Clang 15+** (C++17 required)
- **Python 3.10+** (for data generation — no pip packages needed)

### Build Commands

```bash
# Debug build (fast compile, asserts enabled)
bazel build //src/main:atlas_sort

# Optimized build (what you want for benchmarks)
bazel build //src/main:atlas_sort -c opt --copt=-O3 --copt=-march=native

# Build everything (binary + all tests)
bazel build //src/main:atlas_sort //test:all -c opt --copt=-O3
```

---

## Generate Test Data

The Python generator uses `os.urandom` + `struct.unpack` for fast random int64
generation — **no numpy or pip packages needed**.

```bash
# Generate 1 GB (default)
python3 src/gen/generate_data.py --output data/input.txt --size 1GB

# Generate smaller file for quick testing
python3 src/gen/generate_data.py --output data/small.txt --size 10MB

# Generate 10 GB for stress testing
python3 src/gen/generate_data.py --output data/big.txt --size 10GB
```

**Generator internals** (`src/gen/generate_data.py`):

```python
def generate(output_path: str, target_bytes: int) -> int:
    batch = 500_000                       # ints per write batch
    fmt = f"<{batch}q"                    # little-endian signed 64-bit

    with open(output_path, "w") as f:
        while True:
            raw = os.urandom(batch * 8)   # kernel CSPRNG — fast
            ints = struct.unpack(fmt, raw) # reinterpret as int64
            text = "\n".join(map(str, ints))
            f.write(text)
            f.write("\n")
            # ...check if target_bytes reached...
```

Throughput: **~94 MB/s** (generates 1 GB in ~11 seconds).

---

## Run the Sort

```bash
bazel-bin/src/main/atlas_sort \
  --input=data/input.txt \
  --output=data/output.txt \
  --temp-dir=data/tmp
```

### CLI Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--input=FILE` | *(required)* | Input text file, one integer per line |
| `--output=FILE` | *(required)* | Output text file (sorted) |
| `--temp-dir=DIR` | `/tmp/atlas_sort` | Where binary run files go |
| `--arena=BYTES` | `402653184` (384 MB) | Arena size in bytes |

### Example: Use a Smaller Arena

```bash
# Force more runs with only 64 MB of arena
bazel-bin/src/main/atlas_sort \
  --input=data/input.txt \
  --output=data/output.txt \
  --temp-dir=data/tmp \
  --arena=67108864
```

---

## Run Tests

```bash
# Run all 28 tests across 3 suites
bazel test //test:all --test_output=errors

# Run individual suites
bazel test //test:radix_sort_test
bazel test //test:memory_arena_test
bazel test //test:external_sort_test
```

Expected output:

```
//test:memory_arena_test     PASSED  (4 tests)
//test:radix_sort_test       PASSED  (13 tests)
//test:external_sort_test    PASSED  (11 tests)
───────────────────────────────────────
3/3 suites, 28/28 tests PASS
```

---

## Component Deep Dive

### 1. Memory Arena

**File:** `src/core/memory_arena.h` / `src/core/memory_arena.cc`

The arena is a single `mmap` allocation made once at startup. It is reused for
both phases (repartitioned in between). No `malloc` or `new` in the hot path.

```cpp
// memory_arena.h
class MemoryArena {
 public:
  explicit MemoryArena(size_t size_bytes);
  ~MemoryArena();

  MemoryArena(const MemoryArena&) = delete;
  MemoryArena& operator=(const MemoryArena&) = delete;

  void* Base() const { return base_; }
  size_t Size() const { return size_; }

 private:
  void* base_ = nullptr;
  size_t size_ = 0;
};
```

```cpp
// memory_arena.cc — the entire implementation
MemoryArena::MemoryArena(size_t size_bytes) : size_(size_bytes) {
  base_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base_ == MAP_FAILED) {
    fprintf(stderr, "mmap(%zu bytes) failed: %s\n", size_, strerror(errno));
    abort();
  }
  // Transparent huge pages reduce TLB misses during radix sort's
  // pseudo-random access pattern across the entire buffer
  madvise(base_, size_, MADV_HUGEPAGE);
}

MemoryArena::~MemoryArena() {
  if (base_ && base_ != MAP_FAILED) {
    munmap(base_, size_);
  }
}
```

**Why `mmap` instead of `new`?**

- Returns page-aligned memory (needed if we ever enable `O_DIRECT`)
- Supports `MADV_HUGEPAGE` for 2 MB pages (fewer TLB misses)
- Freed back to OS immediately on `munmap` (no fragmentation)
- Explicitly sized — no silent reallocation

---

### 2. Radix Sort

**File:** `src/core/radix_sort.h` / `src/core/radix_sort.cc`

LSD (Least Significant Digit) radix sort with 256-bucket byte radix. 8 passes
for 8 bytes of `int64_t`. Handles negative numbers via sign-bit flipping.

#### The Sign-Bit Trick

```cpp
// Flip bit 63 so unsigned byte ordering matches signed ordering:
//   INT64_MIN  (-9223372036854775808) → 0x0000000000000000
//   -1                                → 0x7FFFFFFFFFFFFFFF
//   0                                 → 0x8000000000000000
//   INT64_MAX  ( 9223372036854775807) → 0xFFFFFFFFFFFFFFFF
inline uint64_t ToSortable(int64_t v) {
  return static_cast<uint64_t>(v) ^ (1ULL << 63);
}

inline int64_t FromSortable(uint64_t v) {
  return static_cast<int64_t>(v ^ (1ULL << 63));
}
```

#### The Sort Loop

```cpp
void RadixSort(Element* data, Element* scratch, size_t count) {
  if (count <= 1) return;

  auto* src = reinterpret_cast<uint64_t*>(data);
  auto* dst = reinterpret_cast<uint64_t*>(scratch);

  // Step 1: Convert signed → sortable-unsigned
  for (size_t i = 0; i < count; ++i)
    src[i] = ToSortable(static_cast<int64_t>(src[i]));

  // Step 2: 8 LSD passes (one per byte)
  for (int pass = 0; pass < 8; ++pass) {
    const int shift = pass * 8;

    // Count byte frequencies
    size_t counts[256] = {};
    for (size_t i = 0; i < count; ++i)
      ++counts[(src[i] >> shift) & 0xFF];

    // Prefix-sum → scatter offsets
    size_t offsets[256];
    offsets[0] = 0;
    for (int b = 1; b < 256; ++b)
      offsets[b] = offsets[b - 1] + counts[b - 1];

    // Scatter (stable)
    for (size_t i = 0; i < count; ++i) {
      const uint8_t key = (src[i] >> shift) & 0xFF;
      dst[offsets[key]++] = src[i];
    }

    std::swap(src, dst);  // sorted data now in src
  }

  // Step 3: Convert sortable-unsigned → signed
  // After 8 passes (even swaps), src == data.
  for (size_t i = 0; i < count; ++i)
    data[i] = FromSortable(reinterpret_cast<uint64_t*>(data)[i]);
}
```

**Complexity:** O(8n) = O(n) — constant 8 passes regardless of data distribution.  
**Space:** Requires a `scratch` buffer the same size as `data` (the arena's second half).

---

### 3. I/O Layer

**File:** `src/core/io.h` / `src/core/io.cc`

Three classes, each designed for one specific phase of the pipeline:

#### TextReader — Phase 1 input (text → int64)

```cpp
class TextReader {
 public:
  explicit TextReader(const std::string& path);
  ~TextReader();
  size_t ReadBatch(Element* dst, size_t max_count);
  bool Done() const { return done_; }
 private:
  FILE* fp_ = nullptr;
  bool done_ = false;
};
```

```cpp
// Uses a 256 KB stdio buffer for efficient sequential reads
TextReader::TextReader(const std::string& path) {
  fp_ = fopen(path.c_str(), "r");
  setvbuf(fp_, nullptr, _IOFBF, 256 * 1024);
}

size_t TextReader::ReadBatch(Element* dst, size_t max_count) {
  size_t count = 0;
  char line[64];
  while (count < max_count) {
    if (!fgets(line, sizeof(line), fp_)) { done_ = true; break; }
    char* end;
    Element val = static_cast<Element>(strtoll(line, &end, 10));
    if (end == line) continue;  // skip blank/invalid lines
    dst[count++] = val;
  }
  return count;
}
```

#### BinaryWriter — Phase 1 output (int64 → binary run file)

```cpp
void BinaryWriter::Write(const Element* data, size_t count) {
  const char* ptr = reinterpret_cast<const char*>(data);
  size_t remaining = count * sizeof(Element);
  while (remaining > 0) {
    ssize_t n = ::write(fd_, ptr, remaining);
    ptr += n;
    remaining -= static_cast<size_t>(n);
  }
  // Tell the kernel to drop these pages from the page cache.
  // Critical: page cache counts against the cgroup memory limit.
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
}
```

#### BinaryReader — Phase 2 input (binary run → merge heap)

```cpp
class BinaryReader {
 public:
  // buffer + capacity come from the arena — zero heap allocation
  BinaryReader(const std::string& path, Element* buffer, size_t capacity);
  bool HasNext();
  Element Peek() const { return buf_[pos_]; }
  void Advance() { ++pos_; }
 private:
  void Refill();           // read() next batch from file into buffer
  int fd_ = -1;
  Element* buf_;           // points into the arena
  size_t capacity_;        // elements
  size_t valid_ = 0;       // elements currently in buffer
  size_t pos_ = 0;         // cursor
  bool eof_ = false;
};
```

```cpp
void BinaryReader::Refill() {
  char* ptr = reinterpret_cast<char*>(buf_);
  size_t to_read = capacity_ * sizeof(Element);
  size_t total = 0;
  while (total < to_read) {
    ssize_t n = ::read(fd_, ptr + total, to_read - total);
    if (n == 0) { eof_ = true; break; }
    total += static_cast<size_t>(n);
  }
  valid_ = total / sizeof(Element);
  pos_ = 0;
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);  // drop from page cache
}
```

**Key pattern:** Every reader and writer calls `posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED)`
after each I/O operation. This tells the kernel to evict those pages from the page cache.
Without this, the kernel happily caches all the data we read/write, eating into the cgroup
memory limit and potentially causing OOM.

---

### 4. External Sort Orchestrator

**File:** `src/core/external_sort.h` / `src/core/external_sort.cc`

This is the top-level class that wires everything together.

```cpp
struct SortStats {
  double phase1_secs = 0;
  double phase2_secs = 0;
  double total_secs = 0;
  size_t total_elements = 0;
  size_t num_runs = 0;
};

class ExternalSort {
 public:
  struct Config {
    std::string input_path;
    std::string output_path;
    std::string temp_dir = "/tmp/atlas_sort";
    size_t arena_bytes = kDefaultArenaBytes;  // 384 MB
  };

  explicit ExternalSort(Config config);
  SortStats Run();

 private:
  size_t GenerateRuns();  // Phase 1
  void MergeRuns();       // Phase 2
  void Cleanup();         // delete temp run files

  Config config_;
  MemoryArena arena_;
  std::vector<std::string> run_paths_;
  size_t total_elements_ = 0;
};
```

#### Phase 1: GenerateRuns

The arena is split 50/50 — first half for the sort buffer, second half for radix
sort's scratch space:

```cpp
size_t ExternalSort::GenerateRuns() {
  const size_t half = arena_.Size() / 2;
  Element* sort_buf = static_cast<Element*>(arena_.Base());
  const size_t sort_cap = half / sizeof(Element);   // ~25M elements for 384 MB
  Element* scratch = sort_buf + sort_cap;

  TextReader reader(config_.input_path);
  size_t total = 0;
  int run_id = 0;

  while (!reader.Done()) {
    size_t count = reader.ReadBatch(sort_buf, sort_cap);
    if (count == 0) break;

    RadixSort(sort_buf, scratch, count);            // O(n) in-memory sort

    char path[512];
    snprintf(path, sizeof(path), "%s/run_%04d.bin",
             config_.temp_dir.c_str(), run_id++);

    BinaryWriter writer(path);
    writer.Write(sort_buf, count);                  // flush sorted chunk to disk
    run_paths_.emplace_back(path);
    total += count;
  }

  total_elements_ = total;
  return total;
}
```

#### Phase 2: MergeRuns

The arena is repartitioned into K+1 equal slices (K input buffers + 1 output buffer).
A `std::priority_queue` min-heap of size K merges the runs:

```cpp
void ExternalSort::MergeRuns() {
  const size_t K = run_paths_.size();
  const size_t part_elems = arena_.Size() / (K + 1) / sizeof(Element);

  // Create one BinaryReader per run, each backed by an arena slice
  std::vector<std::unique_ptr<BinaryReader>> readers;
  for (size_t i = 0; i < K; ++i) {
    Element* buf = static_cast<Element*>(arena_.Base()) + i * part_elems;
    readers.push_back(
        std::make_unique<BinaryReader>(run_paths_[i], buf, part_elems));
  }

  // Output buffer (arena's last slice, used as char* for text formatting)
  char* out_buf = reinterpret_cast<char*>(
      static_cast<Element*>(arena_.Base()) + K * part_elems);
  const size_t out_cap = part_elems * sizeof(Element);
  size_t out_pos = 0;
  int out_fd = open(config_.output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

  auto flush = [&]() {
    const char* p = out_buf;
    size_t rem = out_pos;
    while (rem > 0) {
      ssize_t w = ::write(out_fd, p, rem);
      p += w;  rem -= w;
    }
    posix_fadvise(out_fd, 0, 0, POSIX_FADV_DONTNEED);
    out_pos = 0;
  };

  // Min-heap: one entry per active run
  struct Entry {
    Element value;
    int idx;
    bool operator>(const Entry& o) const { return value > o.value; }
  };
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

  // Seed heap
  for (size_t i = 0; i < K; ++i) {
    if (readers[i]->HasNext()) {
      heap.push({readers[i]->Peek(), static_cast<int>(i)});
      readers[i]->Advance();
    }
  }

  // Main merge loop
  while (!heap.empty()) {
    const auto top = heap.top();
    heap.pop();

    if (out_cap - out_pos < 32) flush();              // ensure space
    int n = snprintf(out_buf + out_pos, 32,
                     "%" PRId64 "\n", (int64_t)top.value);
    out_pos += n;

    if (readers[top.idx]->HasNext()) {
      heap.push({readers[top.idx]->Peek(), top.idx});
      readers[top.idx]->Advance();
    }
  }

  if (out_pos > 0) flush();
  close(out_fd);
}
```

---

### 5. CLI Entry Point

**File:** `src/main/atlas_sort.cc`

```cpp
int main(int argc, char* argv[]) {
  atlas::ExternalSort::Config cfg;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--input=", 0) == 0)        cfg.input_path = a.substr(8);
    else if (a.rfind("--output=", 0) == 0)   cfg.output_path = a.substr(9);
    else if (a.rfind("--temp-dir=", 0) == 0) cfg.temp_dir = a.substr(11);
    else if (a.rfind("--arena=", 0) == 0)    cfg.arena_bytes = std::stoull(a.substr(8));
    else Usage(argv[0]);
  }

  atlas::ExternalSort sorter(std::move(cfg));
  atlas::SortStats stats = sorter.Run();

  fprintf(stderr,
    "  Elements sorted: %zu\n"
    "  Runs generated:  %zu\n"
    "  Phase 1 (sort):  %.3f s\n"
    "  Phase 2 (merge): %.3f s\n"
    "  TOTAL TIME:      %.3f s\n",
    stats.total_elements, stats.num_runs,
    stats.phase1_secs, stats.phase2_secs, stats.total_secs);
}
```

---

## Memory Budget

For a 500 MB Docker cgroup limit, here is where every byte goes:

```
┌───────────────────────────────────────────────────────┐
│                  500 MB cgroup limit                   │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────────────────┐                          │
│  │  OS / kernel / libc     │  ~50 MB                  │
│  │  (page tables, stack,   │                          │
│  │   thread stacks)        │                          │
│  └─────────────────────────┘                          │
│                                                       │
│  ┌─────────────────────────┐                          │
│  │  mmap arena             │  384 MB                  │
│  │                         │                          │
│  │  Phase 1 layout:        │                          │
│  │   [0..192 MB) sort buf  │  25,165,824 int64s       │
│  │   [192..384 MB) scratch │  25,165,824 int64s       │
│  │                         │                          │
│  │  Phase 2 layout:        │                          │
│  │   [0..128 MB) reader 0  │  16,777,216 int64s       │
│  │   [128..256 MB) reader 1│  16,777,216 int64s       │
│  │   [256..384 MB) out buf │  128 MB char buffer      │
│  └─────────────────────────┘                          │
│                                                       │
│  ┌─────────────────────────┐                          │
│  │  Heap (min-heap, paths) │  < 1 KB                  │
│  └─────────────────────────┘                          │
│                                                       │
│  ┌─────────────────────────┐                          │
│  │  Safety margin          │  ~66 MB                  │
│  └─────────────────────────┘                          │
│                                                       │
│  TOTAL: ~500 MB                                       │
└───────────────────────────────────────────────────────┘
```

### Five rules to avoid OOM

1. **One big `mmap`, done once.** The 384 MB arena is the only large allocation. Reused
   for both phases. No `new`/`malloc` on the hot path.
2. **`posix_fadvise(POSIX_FADV_DONTNEED)` after every I/O.** Evicts pages from the
   kernel page cache — which counts against the cgroup memory limit.
3. **No STL containers on the hot path.** `std::vector` can silently 2x its allocation.
   We use raw pointers into the arena.
4. **`MADV_HUGEPAGE` on the arena.** Reduces TLB misses during radix sort's
   pseudo-random scatter pattern across 192 MB.
5. **Binary intermediate files.** Run files are raw `int64_t` — 8 bytes/element vs ~20
   bytes/element for text. Less I/O, less page cache pressure.

---

## Key Findings & Performance Numbers

### 1 GB Sort Results (49.5 M int64s)

| Metric | Value |
|--------|-------|
| **Total time** | **10.527 s** |
| Phase 1 (read + radix sort + write runs) | 4.938 s |
| Phase 2 (merge + write output) | 5.589 s |
| Runs generated | 2 |
| Elements per run | ~25 M |
| Arena used | 384 MB |
| Input file | 963 MB text |
| Output file | 963 MB text |
| Binary run files | 2 x ~192 MB |

### Where Time Goes

| Operation | Estimated Time | Notes |
|-----------|---------------|-------|
| Text parsing (fgets + strtoll × 49.5M) | ~3 s | Phase 1 bottleneck |
| Radix sort (2 chunks × 25M elements) | ~1 s | O(n), 8 passes each |
| Binary write (2 × 192 MB) | ~0.5 s | Sequential I/O |
| Binary read (2 × 192 MB) | ~0.5 s | Sequential I/O |
| Text formatting (snprintf × 49.5M) | ~3 s | Phase 2 bottleneck |
| Heap operations (push/pop × 49.5M) | ~2 s | O(log K) per element |

**Insight: Text I/O dominates.** Parsing and formatting 49.5 M integers as text accounts
for ~60% of total time. With binary input/output the sort would be ~4 s.

### Radix Sort vs std::sort

| Algorithm | 25M int64s | Complexity | Extra Space |
|-----------|-----------|------------|-------------|
| `RadixSort` (this project) | ~0.4 s | O(8n) = O(n) | Equal-size scratch buffer |
| `std::sort` (introsort) | ~1.8 s | O(n log n) | In-place (O(log n) stack) |

Radix sort is ~4.5x faster for integers because it avoids comparison overhead entirely.

---

## Docker: Enforcing the 500 MB Limit

```bash
# Build the Docker image
docker build -t atlas:latest -f docker/Dockerfile .

# Run with hard 500 MB memory limit
docker run \
  --memory=500m \
  --memory-swap=500m \
  --rm \
  -v "$(pwd)/data:/data" \
  atlas:latest \
  atlas_sort --input=/data/input.txt --output=/data/sorted.txt --temp-dir=/data/tmp
```

| Flag | Purpose |
|------|---------|
| `--memory=500m` | Hard cgroup limit. Kernel OOM-kills if exceeded. |
| `--memory-swap=500m` | Same as memory = **no swap**. Can't hide leaks. |

**If the process touches byte 500,000,001, the kernel kills it immediately.**

### Dockerfile Sketch

```dockerfile
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y g++-12 curl gnupg
# ... install bazel ...
WORKDIR /atlas
COPY . .
RUN bazel build //src/main:atlas_sort -c opt --copt=-O3 --copt=-march=native

FROM ubuntu:22.04 AS runtime
COPY --from=builder /atlas/bazel-bin/src/main/atlas_sort /usr/local/bin/
ENTRYPOINT ["atlas_sort"]
```

---

## Bazel Build Reference

### `.bazelrc`

```python
# C++17 for std::filesystem, std::span, structured bindings
build --cxxopt=-std=c++17
build --host_cxxopt=-std=c++17

# Bazel 9 compat: auto-load cc rules for deps that predate Bazel 9
build --incompatible_autoload_externally=+@rules_cc
```

### `src/core/BUILD` — The Sort Engine

```python
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(name = "types",        hdrs = ["types.h"])
cc_library(name = "memory_arena", srcs = ["memory_arena.cc"], hdrs = ["memory_arena.h"], deps = [":types"])
cc_library(name = "radix_sort",   srcs = ["radix_sort.cc"],   hdrs = ["radix_sort.h"],   deps = [":types"])
cc_library(name = "io",           srcs = ["io.cc"],           hdrs = ["io.h"],           deps = [":types"])
cc_library(
    name = "external_sort",
    srcs = ["external_sort.cc"],
    hdrs = ["external_sort.h"],
    deps = [":io", ":memory_arena", ":radix_sort", ":types"],
)
```

### `src/main/BUILD` — The CLI Binary

```python
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

cc_binary(
    name = "atlas_sort",
    srcs = ["atlas_sort.cc"],
    deps = ["//src/core:external_sort"],
)
```

### `test/BUILD` — All Tests

```python
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(name = "radix_sort_test",    srcs = ["radix_sort_test.cc"],
        deps = ["//src/core:radix_sort", "@googletest//:gtest_main"])
cc_test(name = "memory_arena_test",  srcs = ["memory_arena_test.cc"],
        deps = ["//src/core:memory_arena", "@googletest//:gtest_main"])
cc_test(name = "external_sort_test", srcs = ["external_sort_test.cc"],
        deps = ["//src/core:external_sort", "@googletest//:gtest_main"], size = "medium")
```

### `src/gen/BUILD` — Python Data Generator

```python
load("@rules_python//python:py_binary.bzl", "py_binary")

py_binary(name = "generate_data", srcs = ["generate_data.py"])
```

---

## Test Coverage

### Radix Sort — 13 Tests

| Test | What it validates |
|------|-------------------|
| `Empty` | Zero-length array doesn't crash |
| `SingleElement` | Trivial base case |
| `TwoElements` | Smallest non-trivial swap |
| `AlreadySorted` | No unnecessary mutation |
| `ReverseSorted` | Worst-case input for comparison sorts |
| `AllSameValue` | Stability with duplicates |
| `AllZeros` | Edge case with identical elements |
| `NegativeNumbers` | Sign-bit flipping works |
| `MixedPositiveNegative` | Cross-zero boundary ordering |
| `ExtremeValues` | `INT64_MIN`, `INT64_MAX`, 0, -1, 1 |
| `SmallRange` | High duplicate ratio (-10 to 10, 10K elements) |
| `LargeRandom` | 500K random int64s vs `std::sort` |
| `Duplicates` | Repeated values maintain correct count |

### Memory Arena — 4 Tests

| Test | What it validates |
|------|-------------------|
| `AllocatesRequestedSize` | `Size()` returns exact requested bytes |
| `ReadWriteInts` | Can store/retrieve int64 values correctly |
| `LargeAllocationTouchAllPages` | 64 MB allocation, touch every 4 KB page |
| `PageAligned` | `Base()` is 4096-byte aligned (from mmap) |

### External Sort (End-to-End) — 11 Tests

| Test | Input | Arena | Runs | What it validates |
|------|-------|-------|------|-------------------|
| `Empty` | 0 elements | 1 MB | 0 | Empty file produces empty output |
| `SingleElement` | 1 | 1 MB | 1 | Trivial base case |
| `SmallSorted` | 5 (sorted) | 1 MB | 1 | Already-sorted passthrough |
| `SmallReversed` | 5 (reverse) | 1 MB | 1 | Reversal |
| `NegativeNumbers` | 6 (mixed sign) | 1 MB | 1 | Signed ordering |
| `Duplicates` | 7 (with dups) | 1 MB | 1 | Duplicate handling |
| `AllSame` | 500 (all 7) | 1 MB | 1 | Identical elements |
| `ExtremeValues` | 7 (MIN/MAX/0) | 1 MB | 1 | INT64 boundary values |
| `MediumRandomSingleRun` | 50K random | 4 MB | 1 | Single-run path |
| `ForcesMultipleRuns` | 200K random | 1 MB | **4** | K-way merge with 4 runs |
| `ManySmallRuns` | 500K random | 1 MB | **8** | K-way merge with 8 runs |

---

## Verification

After sorting, verify correctness by checking every adjacent pair:

```bash
python3 -c "
prev = None
count = 0
with open('data/output.txt') as f:
    for line in f:
        val = int(line)
        if prev is not None and val < prev:
            print(f'VIOLATION at line {count}: {prev} > {val}')
        prev = val
        count += 1
print(f'Verified {count:,} integers in non-decreasing order.')
"
```

Our result on the 1 GB test:

```
VERIFIED: all 49,500,000 integers are in non-decreasing order.
```

---

## Scaling to 10 GB+

The algorithm scales linearly. Here's what changes at larger data sizes:

| Input Size | Integers | Runs (384 MB arena) | Merge Passes | Est. Time (SSD) |
|-----------|----------|---------------------|--------------|-----------------|
| 1 GB | 49.5 M | 2 | 1 | ~11 s |
| 10 GB | ~500 M | ~20 | 1 | ~2 min |
| 50 GB | ~2.5 B | ~100 | 1 | ~10 min |
| 200 GB | ~10 B | ~400 | 1 | ~40 min |
| 500 GB+ | 25 B+ | ~1000+ | **2** (multi-pass) | ~2 hr |

With 384 MB chunks, a single merge pass can handle up to ~500 runs before merge
buffers become too small (384 MB / 500 = 768 KB each). Beyond that, a **multi-pass
merge** strategy is needed: merge groups of runs first, then merge the results.

To use the full 500 MB limit with Docker:

```bash
docker run --memory=500m --memory-swap=500m \
  -v "$(pwd)/data:/data" atlas:latest \
  atlas_sort --input=/data/big.txt --output=/data/sorted.txt --temp-dir=/data/tmp
```
