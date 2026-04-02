#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace atlas {

// ═══════════════════════════════════════════════════════════════════════════
// BinaryWriter
// ═══════════════════════════════════════════════════════════════════════════

BinaryWriter::BinaryWriter(const std::string& path) {
  fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_ < 0) {
    fprintf(stderr, "BinaryWriter: cannot open '%s': %s\n",
            path.c_str(), strerror(errno));
    abort();
  }
}

BinaryWriter::~BinaryWriter() {
  if (fd_ >= 0) close(fd_);
}

void BinaryWriter::Write(const Record* data, size_t count) {
  const char* ptr = reinterpret_cast<const char*>(data);
  size_t remaining = count * sizeof(Record);
  while (remaining > 0) {
    ssize_t n = ::write(fd_, ptr, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      fprintf(stderr, "BinaryWriter::Write: %s\n", strerror(errno));
      abort();
    }
    ptr += n;
    remaining -= static_cast<size_t>(n);
  }
#ifdef __linux__
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// BinaryReader
// ═══════════════════════════════════════════════════════════════════════════

BinaryReader::BinaryReader(const std::string& path,
                           Record* buffer, size_t capacity)
    : buf_(buffer), capacity_(capacity) {
  fd_ = open(path.c_str(), O_RDONLY);
  if (fd_ < 0) {
    fprintf(stderr, "BinaryReader: cannot open '%s': %s\n",
            path.c_str(), strerror(errno));
    abort();
  }
#ifdef __linux__
  posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
  Refill();
}

BinaryReader::~BinaryReader() {
  if (fd_ >= 0) close(fd_);
}

bool BinaryReader::HasNext() {
  if (pos_ < valid_) return true;
  if (eof_) return false;
  Refill();
  return pos_ < valid_;
}

void BinaryReader::Refill() {
  char* ptr = reinterpret_cast<char*>(buf_);
  size_t to_read = capacity_ * sizeof(Record);
  size_t total = 0;
  while (total < to_read) {
    ssize_t n = ::read(fd_, ptr + total, to_read - total);
    if (n < 0) {
      if (errno == EINTR) continue;
      fprintf(stderr, "BinaryReader::Refill: %s\n", strerror(errno));
      abort();
    }
    if (n == 0) { eof_ = true; break; }
    total += static_cast<size_t>(n);
  }
  valid_ = total / sizeof(Record);
  pos_ = 0;
#ifdef __linux__
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// LabelReader
// ═══════════════════════════════════════════════════════════════════════════

LabelReader::LabelReader(const std::string& path,
                         Label* buffer, size_t capacity)
    : buf_(buffer), capacity_(capacity) {
  fd_ = open(path.c_str(), O_RDONLY);
  if (fd_ < 0) {
    fprintf(stderr, "LabelReader: cannot open '%s': %s\n",
            path.c_str(), strerror(errno));
    abort();
  }
#ifdef __linux__
  posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
  Refill();
}

LabelReader::~LabelReader() {
  if (fd_ >= 0) close(fd_);
}

bool LabelReader::HasNext() {
  if (pos_ < valid_) return true;
  if (eof_) return false;
  Refill();
  return pos_ < valid_;
}

void LabelReader::Refill() {
  char* ptr = reinterpret_cast<char*>(buf_);
  size_t to_read = capacity_ * sizeof(Label);
  size_t total = 0;
  while (total < to_read) {
    ssize_t n = ::read(fd_, ptr + total, to_read - total);
    if (n < 0) {
      if (errno == EINTR) continue;
      fprintf(stderr, "LabelReader::Refill: %s\n", strerror(errno));
      abort();
    }
    if (n == 0) { eof_ = true; break; }
    total += static_cast<size_t>(n);
  }
  valid_ = total / sizeof(Label);
  pos_ = 0;
#ifdef __linux__
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// MmapReader (Prompt 4: Extreme I/O)
// ═══════════════════════════════════════════════════════════════════════════

MmapReader::MmapReader(const std::string& path) {
  fd_ = open(path.c_str(), O_RDONLY);
  if (fd_ < 0) {
    fprintf(stderr, "MmapReader: cannot open '%s': %s\n",
            path.c_str(), strerror(errno));
    abort();
  }

  struct stat st;
  if (fstat(fd_, &st) < 0) {
    perror("fstat");
    abort();
  }
  size_bytes_ = st.st_size;
  total_records_ = size_bytes_ / sizeof(Record);

  if (size_bytes_ > 0) {
    ptr_ = static_cast<Record*>(mmap(nullptr, size_bytes_, PROT_READ, MAP_SHARED, fd_, 0));
    if (ptr_ == MAP_FAILED) {
      perror("mmap");
      abort();
    }
    
    madvise(ptr_, size_bytes_, MADV_SEQUENTIAL);
  }
}

MmapReader::~MmapReader() {
  if (ptr_ && ptr_ != MAP_FAILED) {
    madvise(ptr_, size_bytes_, MADV_DONTNEED);
    munmap(ptr_, size_bytes_);
  }
  if (fd_ >= 0) close(fd_);
}

}  // namespace atlas
