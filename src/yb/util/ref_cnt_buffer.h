//
// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <string>

#include "yb/util/slice.h"

namespace yb {

class faststring;

// Byte buffer with reference counting. It embeds reference count, size and data in a single block.
class RefCntBuffer {
 public:
  RefCntBuffer();
  explicit RefCntBuffer(size_t size);
  RefCntBuffer(const char *data, size_t size);

  RefCntBuffer(const char *data, const char *end)
      : RefCntBuffer(data, end - data) {}

  RefCntBuffer(const uint8_t *data, size_t size)
      : RefCntBuffer(static_cast<const char*>(static_cast<const void*>(data)), size) {}

  explicit RefCntBuffer(const std::string& str) :
      RefCntBuffer(str.c_str(), str.length()) {}

  explicit RefCntBuffer(const faststring& str);

  explicit RefCntBuffer(Slice slice) :
      RefCntBuffer(slice.data(), slice.size()) {}

  RefCntBuffer(const RefCntBuffer& rhs) noexcept;
  RefCntBuffer(RefCntBuffer&& rhs) noexcept;

  void operator=(const RefCntBuffer& rhs) noexcept;
  void operator=(RefCntBuffer&& rhs) noexcept;

  ~RefCntBuffer();

  size_t size() const {
    return size_reference();
  }

  size_t DynamicMemoryUsage() const { return data_ ? GetInternalBufSize(size()) : 0; }

  bool empty() const {
    return size() == 0;
  }

  char* data() const {
    return data_ + sizeof(CounterType) + sizeof(size_t);
  }

  char* begin() const {
    return data();
  }

  char* end() const {
    return begin() + size();
  }

  uint8_t* udata() const {
    return static_cast<unsigned char*>(static_cast<void*>(data()));
  }

  uint8_t* ubegin() const {
    return udata();
  }

  uint8_t* uend() const {
    return udata() + size();
  }

  void Reset() { DoReset(nullptr); }

  explicit operator bool() const {
    return data_ != nullptr;
  }

  bool operator!() const {
    return data_ == nullptr;
  }

  std::string ToBuffer() const {
    return std::string(begin(), end());
  }

  Slice AsSlice() const {
    return Slice(data(), size());
  }

  Slice as_slice() const __attribute__ ((deprecated)) {
    return Slice(data(), size());
  }

  void Shrink(size_t new_size) {
    size_reference() = new_size;
  }

  bool unique() const {
    return counter_reference().load(std::memory_order_acquire) == 1;
  }

 private:
  void DoReset(char* data);

  static size_t GetInternalBufSize(size_t data_size);

  // Using ptrdiff_t since it matches register size and is signed.
  typedef std::atomic<std::ptrdiff_t> CounterType;

  size_t& size_reference() const {
    return *static_cast<size_t*>(static_cast<void*>(data_ + sizeof(CounterType)));
  }

  CounterType& counter_reference() const {
    return *static_cast<CounterType*>(static_cast<void*>(data_));
  }

  char *data_;
};

struct RefCntBufferHash {
  size_t operator()(const RefCntBuffer& inp) const {
    return inp.as_slice().hash();
  }
};

class RefCntPrefix {
 public:
  RefCntPrefix() : size_(0) {}

  explicit RefCntPrefix(const std::string& str)
      : bytes_(RefCntBuffer(str)), size_(bytes_.size()) {}

  explicit RefCntPrefix(Slice slice)
      : bytes_(RefCntBuffer(slice)), size_(bytes_.size()) {}

  RefCntPrefix(RefCntBuffer bytes) // NOLINT
      : bytes_(std::move(bytes)), size_(bytes_.size()) {}

  RefCntPrefix(RefCntBuffer bytes, size_t size)
      : bytes_(std::move(bytes)), size_(size) {}

  RefCntPrefix(const RefCntPrefix& doc_key, size_t size)
      : bytes_(doc_key.bytes_), size_(size) {}

  explicit operator bool() const {
    return static_cast<bool>(bytes_);
  }

  void Resize(size_t value);

  Slice as_slice() const {
    return Slice(bytes_.data(), size_);
  }

  const char* data() const {
    return bytes_.data();
  }

  size_t size() const {
    return size_;
  }

  int Compare(const RefCntPrefix& rhs) const {
    auto my_size = size_;
    auto rhs_size = rhs.size_;
    const size_t min_len = std::min(my_size, rhs_size);
    int r = strings::fastmemcmp_inlined(bytes_.data(), rhs.bytes_.data(), min_len);
    if (r == 0) {
      if (my_size < rhs_size) { return -1; }
      if (my_size > rhs_size) { return 1; }
    }
    return r;
  }

  std::string ToString() const;

 private:
  RefCntBuffer bytes_;
  size_t size_;

  friend inline bool operator<(const RefCntPrefix& lhs, const RefCntPrefix& rhs) {
    return lhs.Compare(rhs) < 0;
  }

  friend inline bool operator==(const RefCntPrefix& lhs, const RefCntPrefix& rhs) {
    return lhs.size_ == rhs.size_ && strings::memeq(lhs.data(), rhs.data(), lhs.size_);
  }
};

struct RefCntPrefixHash {
  size_t operator()(const RefCntPrefix& inp) const {
    return inp.as_slice().hash();
  }
};

class RefCntSlice {
 public:
  RefCntSlice() = default;

  explicit RefCntSlice(RefCntBuffer holder)
      : holder_(std::move(holder)), slice_(holder_.AsSlice()) {}

  RefCntSlice(RefCntBuffer holder, Slice slice)
      : holder_(std::move(holder)), slice_(slice) {}

  explicit operator bool() const {
    return static_cast<bool>(holder_);
  }

  Slice AsSlice() const {
    return slice_;
  }

  bool empty() const {
    return slice_.empty();
  }

  size_t size() const {
    return slice_.size();
  }

  const uint8_t* udata() const {
    return slice_.data();
  }

  const char* data() const {
    return slice_.cdata();
  }

  uint8_t* data() {
    return slice_.mutable_data();
  }

  uint8_t* end() {
    return slice_.mutable_data() + slice_.size();
  }

  size_t SpaceAfterSlice() const {
    return holder_.AsSlice().end() - slice_.end();
  }

  void Grow(size_t delta) {
    slice_ = Slice(slice_.data(), slice_.end() + delta);
  }

  bool unique() const {
    return holder_.unique();
  }

  void Resize(size_t new_size) {
    slice_ = Slice(slice_.mutable_data(), new_size);
  }

  std::string ToString() const;

 private:
  RefCntBuffer holder_;
  Slice slice_;

  friend inline auto operator<=>(const RefCntSlice& lhs, const RefCntSlice& rhs) {
    return lhs.AsSlice() <=> rhs.AsSlice();
  }

  friend inline bool operator==(const RefCntSlice& lhs, const RefCntSlice& rhs) {
    return lhs.AsSlice() == rhs.AsSlice();
  }
};

struct RefCntSliceHash {
  size_t operator()(const RefCntSlice& inp) const {
    return inp.AsSlice().hash();
  }
};

} // namespace yb
