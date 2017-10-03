// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Slice is a simple structure containing a pointer into some external
// storage and a size.  The user of a Slice must ensure that the slice
// is not used after the corresponding external storage has been
// deallocated.
//
// Multiple threads can invoke const methods on a Slice without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Slice must use
// external synchronization.
//
// Slices can be built around faststrings and StringPieces using constructors
// with implicit casts. Both StringPieces and faststrings depend on a great
// deal of gutil code, so these constructors are conditionalized on
// KUDU_HEADERS_USE_RICH_SLICE. Likewise, KUDU_HEADERS_USE_RICH_SLICE controls
// whether to use gutil-based memeq/memcmp substitutes; if it is unset, Slice
// will fall back to standard memcmp.

#ifndef KUDU_UTIL_SLICE_H_
#define KUDU_UTIL_SLICE_H_

#include <assert.h>
#include <map>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>

#ifdef KUDU_HEADERS_USE_RICH_SLICE
#include "kudu/gutil/strings/fastmem.h"
#include "kudu/gutil/strings/stringpiece.h"
#include "kudu/util/faststring.h"
#endif
#include "kudu/util/kudu_export.h"

namespace kudu {

class Status;

class KUDU_EXPORT Slice {
 public:
  // Create an empty slice.
  Slice() : data_(reinterpret_cast<const uint8_t *>("")),
            size_(0) { }

  // Create a slice that refers to d[0,n-1].
  Slice(const uint8_t* d, size_t n) : data_(d), size_(n) { }

  // Create a slice that refers to d[0,n-1].
  Slice(const char* d, size_t n) :
    data_(reinterpret_cast<const uint8_t *>(d)),
    size_(n) { }

  // Create a slice that refers to the contents of "s"
  Slice(const std::string& s) : // NOLINT(runtime/explicit)
    data_(reinterpret_cast<const uint8_t *>(s.data())),
    size_(s.size()) { }

  // Create a slice that refers to s[0,strlen(s)-1]
  Slice(const char* s) : // NOLINT(runtime/explicit)
    data_(reinterpret_cast<const uint8_t *>(s)),
    size_(strlen(s)) { }

#ifdef KUDU_HEADERS_USE_RICH_SLICE
  // Create a slice that refers to the contents of the faststring.
  // Note that further appends to the faststring may invalidate this slice.
  Slice(const faststring &s) // NOLINT(runtime/explicit)
    : data_(s.data()),
      size_(s.size()) {
  }

  Slice(const StringPiece& s) // NOLINT(runtime/explicit)
    : data_(reinterpret_cast<const uint8_t*>(s.data())),
      size_(s.size()) {
  }
#endif

  // Return a pointer to the beginning of the referenced data
  const uint8_t* data() const { return data_; }

  // Return a mutable pointer to the beginning of the referenced data.
  uint8_t *mutable_data() { return const_cast<uint8_t *>(data_); }

  // Return the length (in bytes) of the referenced data
  size_t size() const { return size_; }

  // Return true iff the length of the referenced data is zero
  bool empty() const { return size_ == 0; }

  // Return the ith byte in the referenced data.
  // REQUIRES: n < size()
  const uint8_t &operator[](size_t n) const {
    assert(n < size());
    return data_[n];
  }

  // Change this slice to refer to an empty array
  void clear() {
    data_ = reinterpret_cast<const uint8_t *>("");
    size_ = 0;
  }

  // Drop the first "n" bytes from this slice.
  void remove_prefix(size_t n) {
    assert(n <= size());
    data_ += n;
    size_ -= n;
  }

  // Truncate the slice to "n" bytes
  void truncate(size_t n) {
    assert(n <= size());
    size_ = n;
  }

  // Checks that this slice has size() = 'expected_size' and returns
  // Status::Corruption() otherwise.
  Status check_size(size_t expected_size) const;

  // Return a string that contains the copy of the referenced data.
  std::string ToString() const;

  std::string ToDebugString(size_t max_len = 0) const;

  // Three-way comparison.  Returns value:
  //   <  0 iff "*this" <  "b",
  //   == 0 iff "*this" == "b",
  //   >  0 iff "*this" >  "b"
  int compare(const Slice& b) const;

  // Return true iff "x" is a prefix of "*this"
  bool starts_with(const Slice& x) const {
    return ((size_ >= x.size_) &&
            (MemEqual(data_, x.data_, x.size_)));
  }

  // Comparator struct, useful for ordered collections (like STL maps).
  struct Comparator {
    bool operator()(const Slice& a, const Slice& b) const {
      return a.compare(b) < 0;
    }
  };

  // Relocates this slice's data into 'd' provided this isn't already the
  // case. It is assumed that 'd' is large enough to fit the data.
  void relocate(uint8_t* d) {
    if (data_ != d) {
      memcpy(d, data_, size_);
      data_ = d;
    }
  }

 private:
  friend bool operator==(const Slice& x, const Slice& y);

  static bool MemEqual(const void* a, const void* b, size_t n) {
#ifdef KUDU_HEADERS_USE_RICH_SLICE
    return strings::memeq(a, b, n);
#else
    return memcmp(a, b, n) == 0;
#endif
  }

  static int MemCompare(const void* a, const void* b, size_t n) {
#ifdef KUDU_HEADERS_USE_RICH_SLICE
    return strings::fastmemcmp_inlined(a, b, n);
#else
    return memcmp(a, b, n);
#endif
  }

  const uint8_t* data_;
  size_t size_;

  // Intentionally copyable
};

inline bool operator==(const Slice& x, const Slice& y) {
  return ((x.size() == y.size()) &&
          (Slice::MemEqual(x.data(), y.data(), x.size())));
}

inline bool operator!=(const Slice& x, const Slice& y) {
  return !(x == y);
}

inline std::ostream& operator<<(std::ostream& o, const Slice& s) {
  return o << s.ToDebugString(16); // should be enough for anyone...
}

inline int Slice::compare(const Slice& b) const {
  const int min_len = (size_ < b.size_) ? size_ : b.size_;
  int r = MemCompare(data_, b.data_, min_len);
  if (r == 0) {
    if (size_ < b.size_) r = -1;
    else if (size_ > b.size_) r = +1;
  }
  return r;
}

// STL map whose keys are Slices.
//
// See sample usage in slice-test.cc.
template <typename T>
struct SliceMap {
  typedef std::map<Slice, T, Slice::Comparator> type;
};

}  // namespace kudu

#endif  // KUDU_UTIL_SLICE_H_
