#pragma once
#include "types.h"

#include <cstdio>
#include <string>

namespace atlas {

// ─── Phase 1: read text input ──────────────────────────────────────────────

/// Reads integers from a text file (one per line) into Element buffers.
class TextReader {
 public:
  explicit TextReader(const std::string& path);
  ~TextReader();

  /// Fill dst[0..max_count) with parsed integers.  Returns actual count.
  size_t ReadBatch(Element* dst, size_t max_count);

  bool Done() const { return done_; }

 private:
  FILE* fp_ = nullptr;
  bool done_ = false;
};

// ─── Phase 1→2 bridge: binary run files ────────────────────────────────────

/// Writes a contiguous Element array to a binary file.
class BinaryWriter {
 public:
  explicit BinaryWriter(const std::string& path);
  ~BinaryWriter();

  void Write(const Element* data, size_t count);

 private:
  int fd_ = -1;
};

/// Reads Elements from a binary file with caller-provided buffer.
/// Used during the K-way merge — each run gets one BinaryReader.
class BinaryReader {
 public:
  /// buffer/capacity are from the arena — no heap alloc.
  BinaryReader(const std::string& path, Element* buffer, size_t capacity);
  ~BinaryReader();

  bool HasNext();
  Element Peek() const { return buf_[pos_]; }
  void Advance() { ++pos_; }

 private:
  void Refill();

  int fd_ = -1;
  Element* buf_;
  size_t capacity_;   // max elements the buffer can hold
  size_t valid_ = 0;  // elements currently in buffer
  size_t pos_ = 0;    // read cursor within buffer
  bool eof_ = false;
};

}  // namespace atlas
