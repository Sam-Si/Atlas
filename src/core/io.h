#pragma once
#include "types.h"

#include <cstdio>
#include <string>

namespace atlas {

// ─── Phase 1: Binary Input ──────────────────────────────────────────────────

/// Reads Records from the massive input binary file.
class BinaryReader {
 public:
  /// buffer/capacity are from the arena — no heap alloc.
  BinaryReader(const std::string& path, Record* buffer, size_t capacity);
  ~BinaryReader();

  bool HasNext();
  const Record& Peek() const { return buf_[pos_]; }
  void Advance() { ++pos_; }

 private:
  void Refill();

  int fd_ = -1;
  Record* buf_;
  size_t capacity_;   // max elements the buffer can hold
  size_t valid_ = 0;  // elements currently in buffer
  size_t pos_ = 0;    // read cursor within buffer
  bool eof_ = false;
};

/// Reads Labels from the labels binary file.
class LabelReader {
 public:
  LabelReader(const std::string& path, Label* buffer, size_t capacity);
  ~LabelReader();

  bool HasNext();
  const Label& Peek() const { return buf_[pos_]; }
  void Advance() { ++pos_; }

 private:
  void Refill();

  int fd_ = -1;
  Label* buf_;
  size_t capacity_;   // max elements the buffer can hold
  size_t valid_ = 0;  // elements currently in buffer
  size_t pos_ = 0;    // read cursor within buffer
  bool eof_ = false;
};

// ─── Phase 2+: Extreme I/O (mmap) ──────────────────────────────────────────

/// Prompt 4: Use mmap and madvise to bypass standard kernel buffering.
class MmapReader {
 public:
  MmapReader(const std::string& path);
  ~MmapReader();

  bool HasNext() const { return pos_ < total_records_; }
  const Record& Peek() const { return ptr_[pos_]; }
  void Advance() { ++pos_; }

 private:
  int fd_ = -1;
  Record* ptr_ = nullptr;
  size_t size_bytes_ = 0;
  size_t total_records_ = 0;
  size_t pos_ = 0;
};

// ─── Outbound: binary run files ────────────────────────────────────────────

/// Writes a contiguous Record array to a binary file.
class BinaryWriter {
 public:
  explicit BinaryWriter(const std::string& path);
  ~BinaryWriter();

  void Write(const Record* data, size_t count);

 private:
  int fd_ = -1;
};

}  // namespace atlas
