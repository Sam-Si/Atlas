#include "io.h"

#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace atlas {

// ═══════════════════════════════════════════════════════════════════════════
// TextReader
// ═══════════════════════════════════════════════════════════════════════════

TextReader::TextReader(const std::string& path) {
  fp_ = fopen(path.c_str(), "r");
  if (!fp_) {
    fprintf(stderr, "TextReader: cannot open '%s': %s\n",
            path.c_str(), strerror(errno));
    abort();
  }
  // 256 KB stdio buffer for efficient sequential reads
  setvbuf(fp_, nullptr, _IOFBF, 256 * 1024);
}

TextReader::~TextReader() {
  if (fp_) fclose(fp_);
}

size_t TextReader::ReadBatch(Element* dst, size_t max_count) {
  size_t count = 0;
  char line[64];
  while (count < max_count) {
    if (!fgets(line, sizeof(line), fp_)) {
      done_ = true;
      break;
    }
    char* end;
    Element val = static_cast<Element>(strtoll(line, &end, 10));
    if (end == line) continue;  // skip blank / invalid lines
    dst[count++] = val;
  }
  // ── Critical for memory budget ──
  // Without this, the kernel keeps every page of the (up-to 1 GB) input
  // file in the page cache.  Page-cache pages count against the cgroup
  // memory limit, so the process would OOM long before it finishes.
  // FADV_DONTNEED tells the kernel to drop pages we've already consumed.
  posix_fadvise(fileno(fp_), 0, 0, POSIX_FADV_DONTNEED);
  return count;
}

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

void BinaryWriter::Write(const Element* data, size_t count) {
  const char* ptr = reinterpret_cast<const char*>(data);
  size_t remaining = count * sizeof(Element);
  while (remaining > 0) {
    ssize_t n = ::write(fd_, ptr, remaining);
    if (n < 0) {
      fprintf(stderr, "BinaryWriter::Write: %s\n", strerror(errno));
      abort();
    }
    ptr += n;
    remaining -= static_cast<size_t>(n);
  }
  // Drop written pages from page cache to stay within memory budget
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
}

// ═══════════════════════════════════════════════════════════════════════════
// BinaryReader
// ═══════════════════════════════════════════════════════════════════════════

BinaryReader::BinaryReader(const std::string& path,
                           Element* buffer, size_t capacity)
    : buf_(buffer), capacity_(capacity) {
  fd_ = open(path.c_str(), O_RDONLY);
  if (fd_ < 0) {
    fprintf(stderr, "BinaryReader: cannot open '%s': %s\n",
            path.c_str(), strerror(errno));
    abort();
  }
  posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
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
  size_t to_read = capacity_ * sizeof(Element);
  size_t total = 0;
  while (total < to_read) {
    ssize_t n = ::read(fd_, ptr + total, to_read - total);
    if (n < 0) {
      fprintf(stderr, "BinaryReader::Refill: %s\n", strerror(errno));
      abort();
    }
    if (n == 0) { eof_ = true; break; }
    total += static_cast<size_t>(n);
  }
  valid_ = total / sizeof(Element);
  pos_ = 0;
  // Drop read pages from cache
  posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
}

}  // namespace atlas
