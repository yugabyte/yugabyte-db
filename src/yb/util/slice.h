// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
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
// YB_HEADERS_USE_RICH_SLICE. Likewise, YB_HEADERS_USE_RICH_SLICE controls
// whether to use gutil-based memeq/memcmp substitutes; if it is unset, Slice
// will fall back to standard memcmp.

#ifndef YB_UTIL_SLICE_H_
#define YB_UTIL_SLICE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <map>
#include <string>

#ifdef YB_HEADERS_USE_RICH_SLICE
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/util/faststring.h"
#endif
#include "yb/util/cast.h"


namespace yb {

class Status;
struct SliceParts;

class Slice {
 public:
  // Create an empty slice.
  Slice() : data_(reinterpret_cast<const uint8_t *>("")),
            size_(0) { }

  // Create a slice that refers to d[0,n-1].
  Slice(const uint8_t* d, size_t n) : data_(d), size_(n) { }

  // Create a slice that refers to [begin, end).
  Slice(const uint8_t* begin, const uint8_t* end) : data_(begin), size_(end - begin) {
    CHECK_LE(begin, end);
  }

  Slice(const char* begin, const char* end)
      : Slice(util::to_uchar_ptr(begin), util::to_uchar_ptr(end)) {}

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

#ifdef YB_HEADERS_USE_RICH_SLICE
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

  // Create a single slice from SliceParts using buf as storage.
  // buf must exist as long as the returned Slice exists.
  Slice(const SliceParts& parts, std::string* buf);

  const char* cdata() const { return reinterpret_cast<const char*>(data_); }

  // Return a pointer to the beginning of the referenced data
  const uint8_t* data() const { return data_; }

  // Return a mutable pointer to the beginning of the referenced data.
  uint8_t *mutable_data() { return const_cast<uint8_t *>(data_); }

  const uint8_t* end() const { return data_ + size_; }

  const char* cend() const { return reinterpret_cast<const char*>(end()); }

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

  // Drop the last "n" bytes from this slice.
  void remove_suffix(size_t n) {
    assert(n <= size());
    size_ -= n;
  }

  // Truncate the slice to "n" bytes
  void truncate(size_t n) {
    assert(n <= size());
    size_ = n;
  }

  char consume_byte() {
    assert(size_ > 0);
    char c = *data_;
    ++data_;
    --size_;
    return c;
  }

  // Checks that this slice has size() = 'expected_size' and returns
  // STATUS(Corruption, ) otherwise.
  Status check_size(size_t expected_size) const;

  // Compare two slices and returns the offset of the first byte where they differ.
  size_t difference_offset(const Slice& b) const;

  // Return a string that contains the copy of the referenced data.
  std::string ToBuffer() const;

  std::string ToString() const __attribute__ ((deprecated)) { return ToBuffer(); }
  std::string ToString(bool hex) const __attribute__ ((deprecated));

  std::string ToDebugString(size_t max_len = 0) const;
  std::string ToDebugHexString() const;

  // Three-way comparison.  Returns value:
  //   <  0 iff "*this" <  "b",
  //   == 0 iff "*this" == "b",
  //   >  0 iff "*this" >  "b"
  int compare(const Slice& b) const;

  size_t hash() const noexcept;

  // Return true iff "x" is a prefix of "*this"
  bool starts_with(const Slice& x) const {
    return ((size_ >= x.size_) &&
            (MemEqual(data_, x.data_, x.size_)));
  }

  bool ends_with(const Slice& x) const {
    return ((size_ >= x.size_) &&
            (MemEqual(data_ + size_ - x.size_, x.data_, x.size_)));
  }

  // Comparator struct, useful for ordered collections (like STL maps).
  struct Comparator {
    bool operator()(const Slice& a, const Slice& b) const {
      return a.compare(b) < 0;
    }
  };

  struct Hash {
    size_t operator()(const Slice& a) const noexcept {
      return a.hash();
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
#ifdef YB_HEADERS_USE_RICH_SLICE
    return strings::memeq(a, b, n);
#else
    return memcmp(a, b, n) == 0;
#endif
  }

  static int MemCompare(const void* a, const void* b, size_t n) {
#ifdef YB_HEADERS_USE_RICH_SLICE
    return strings::fastmemcmp_inlined(a, b, n);
#else
    return memcmp(a, b, n);
#endif
  }

  const uint8_t* data_;
  size_t size_;

  // Intentionally copyable
};

struct SliceParts {
  SliceParts(const Slice* _parts, int _num_parts) :
      parts(_parts), num_parts(_num_parts) { }
  SliceParts() : parts(nullptr), num_parts(0) {}

  const Slice* parts;
  int num_parts;
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
  const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
  int r = MemCompare(data_, b.data_, min_len);
  if (r == 0) {
    if (size_ < b.size_) r = -1;
    else if (size_ > b.size_) r = +1;
  }
  return r;
}

inline size_t Slice::hash() const noexcept {
  constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
  constexpr uint64_t kFnvPrime = 1099511628211ULL;
  size_t result = kFnvOffset;
  const uint8_t* e = end();
  for (const uint8_t* i = data_; i != e; ++i) {
    result = (result * kFnvPrime) ^ *i;
  }
  return result;
}

inline size_t Slice::difference_offset(const Slice& b) const {
  size_t off = 0;
  const size_t len = (size_ < b.size_) ? size_ : b.size_;
  for (; off < len; off++) {
    if (data_[off] != b.data_[off]) break;
  }
  return off;
}

// STL map whose keys are Slices.
//
// See sample usage in slice-test.cc.
template <typename T>
struct SliceMap {
  typedef std::map<Slice, T, Slice::Comparator> type;
};

}  // namespace yb

namespace rocksdb {

typedef yb::Slice Slice;
typedef yb::SliceParts SliceParts;

}  // namespace rocksdb

#endif // YB_UTIL_SLICE_H_
