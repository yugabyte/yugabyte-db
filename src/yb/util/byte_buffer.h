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

#pragma once

#include <cstddef>
#include <string>

#include "yb/util/mem_tracker.h"
#include "yb/util/slice.h"

namespace yb {

// Utility class to store arbitrary amount of byte with inplace buffer of specified size.
template <size_t SmallLen, bool kTrackMemoryUsage>
class ByteBufferBase {
 public:
  static_assert(SmallLen >= sizeof(void*), "Too small buffer");

  ByteBufferBase() requires (kTrackMemoryUsage == false) : size_(0) {}

  explicit ByteBufferBase(const MemTrackerPtr& mem_tracker)
      : size_(0), consumption_(mem_tracker, 0) {}

  explicit ByteBufferBase(const std::string& str) requires (kTrackMemoryUsage == false) {
    Assign(str.c_str(), str.size());
  }

  ByteBufferBase(const MemTrackerPtr& mem_tracker, const std::string& str)
      : consumption_(mem_tracker, 0) {
    Assign(str.c_str(), str.size());
  }

  void operator=(const std::string& str) {
    Assign(str.c_str(), str.size());
  }

  explicit ByteBufferBase(Slice slice) requires (kTrackMemoryUsage == false) {
    Assign(slice.cdata(), slice.cend());
  }

  ByteBufferBase(const MemTrackerPtr& mem_tracker, Slice slice)
      : consumption_(mem_tracker, 0) {
    Assign(slice.cdata(), slice.cend());
  }

  ByteBufferBase(Slice slice1, Slice slice2) requires (kTrackMemoryUsage == false) {
    Assign(slice1, slice2);
  }

  ByteBufferBase(const MemTrackerPtr& mem_tracker, Slice slice1, Slice slice2)
      : consumption_(mem_tracker, 0) {
    Assign(slice1, slice2);
  }

  void operator=(Slice slice) {
    Assign(slice.cdata(), slice.cend());
  }

  ByteBufferBase(const ByteBufferBase<SmallLen, true>& rhs)
      : consumption_(rhs.consumption_.mem_tracker(), 0) {
    Assign(rhs.ptr(), rhs.size_);
  }

  ByteBufferBase(const ByteBufferBase<SmallLen, false>& rhs) requires (kTrackMemoryUsage == false) {
    Assign(rhs.ptr(), rhs.size_);
  }

  void operator=(const ByteBufferBase<SmallLen, kTrackMemoryUsage>& rhs) {
    Assign(rhs.ptr(), rhs.size_);
  }

  ByteBufferBase(ByteBufferBase<SmallLen, kTrackMemoryUsage>&& rhs) {
    if (!rhs.big()) {
      memcpy(small_buffer_, rhs.small_buffer_, rhs.size_);
    } else {
      capacity_ = rhs.capacity_;
      big_buffer_ = rhs.big_buffer_;
      rhs.capacity_ = SmallLen;
    }

    size_ = rhs.size_;
    rhs.size_ = 0;
    consumption_ = std::move(rhs.consumption_);
  }

  void operator=(ByteBufferBase<SmallLen, kTrackMemoryUsage>&& rhs) {
    if (!rhs.big()) {
      memcpy(ptr(), rhs.small_buffer_, rhs.size_);
    } else {
      if (big()) {
        free(big_buffer_);
      }
      capacity_ = rhs.capacity_;
      big_buffer_ = rhs.big_buffer_;
      rhs.capacity_ = SmallLen;
    }

    size_ = rhs.size_;
    rhs.size_ = 0;
    consumption_ = std::move(rhs.consumption_);
  }

  ~ByteBufferBase() {
    if (big()) {
      free(big_buffer_);
    }
  }

  void Assign(Slice slice1, Slice slice2) {
    auto sum_sizes = slice1.size() + slice2.size();
    auto* out = EnsureCapacity(sum_sizes, 0);
    slice1.CopyTo(out);
    slice2.CopyTo(out + slice1.size());
    size_ = sum_sizes;
  }

  bool empty() const {
    return size_ == 0;
  }

  size_t size() const {
    return size_;
  }

  void Clear() {
    size_ = 0;
  }

  char& Back() {
    return ptr()[size_ - 1];
  }

  char Back() const {
    return ptr()[size_ - 1];
  }

  char& operator[](size_t len) {
    return ptr()[len];
  }

  char operator[](size_t len) const {
    return ptr()[len];
  }

  void PopBack() {
    --size_;
  }

  void Truncate(size_t new_size) {
    size_ = new_size;
  }

  void Assign(Slice slice) {
    Assign(slice.cdata(), slice.cend());
  }

  void Assign(const char* a, const char* b) {
    Assign(a, b - a);
  }

  void Assign(const char* a, size_t size) {
    DoAppend(0, a, size);
  }

  void Append(Slice slice) {
    Append(slice.cdata(), slice.cend());
  }

  template <size_t OtherSmallLen>
  void Append(const ByteBufferBase<OtherSmallLen, kTrackMemoryUsage>& rhs) {
    Append(rhs.ptr(), rhs.size_);
  }

  void Append(const char* a, const char* b) {
    Append(a, b - a);
  }

  void Append(const char* a, size_t size) {
    DoAppend(size_, a, size);
  }

  void AppendWithPrefix(char prefix, Slice data) {
    AppendWithPrefix(prefix, data.cdata(), data.size());
  }

  void AppendWithPrefix(char prefix, const char* data, size_t len) {
    const size_t old_size = size_;
    const size_t new_size = old_size + 1 + len;
    char* out = EnsureCapacity(new_size, old_size) + old_size;
    *out++ = prefix;
    memcpy(out, data, len);
    size_ = new_size;
  }

  void Reserve(size_t capacity) {
    EnsureCapacity(capacity, size_);
  }

  char* GrowByAtLeast(size_t size) {
    size += size_;
    auto result = EnsureCapacity(size, size_) + size_;
    size_ = size;
    return result;
  }

  void PushBack(char ch) {
    EnsureCapacity(size_ + 1, size_)[size_] = ch;
    ++size_;
  }

  std::string ToStringBuffer() const {
    return AsSlice().ToBuffer();
  }

  std::string ToString() const {
    return AsSlice().ToDebugHexString();
  }

  Slice AsSlice() const {
    return Slice(ptr(), size_);
  }

  const uint8_t* data() const {
    return pointer_cast<const uint8_t*>(ptr());
  }

  uint8_t* mutable_data() {
    return pointer_cast<uint8_t*>(ptr());
  }

  const uint8_t* end() const {
    return pointer_cast<const uint8_t*>(ptr()) + size_;
  }

  // STL container compatibility
  void clear() {
    Clear();
  }

  template <class... Args>
  void append(Args&&... args) {
    Append(std::forward<Args>(args)...);
  }

  template <class... Args>
  void assign(Args&&... args) {
    Assign(std::forward<Args>(args)...);
  }

  void reserve(size_t capacity) {
    Reserve(capacity);
  }

  void push_back(char ch) {
    PushBack(ch);
  }

  char& back() {
    return Back();
  }

  char back() const {
    return Back();
  }

  void pop_back() {
    PopBack();
  }

 private:
  void DoConsume(int64_t delta) {
    if constexpr (kTrackMemoryUsage) {
      consumption_.Add(delta);
    }
  }

  void DoAppend(size_t keep_size, const char* a, size_t len) {
    size_t new_size = keep_size + len;
    memcpy(EnsureCapacity(new_size, keep_size) + keep_size, a, len);
    size_ = new_size;
  }

  // Ensures that buffer could contain at least capacity bytes.
  // In case of relocation, keep_size bytes will be copied.
  char* EnsureCapacity(size_t capacity, size_t keep_size) {
    if (capacity <= capacity_) {
      return ptr();
    }

    bool was_big = big();
    const auto old_capacity = capacity_;
    while ((capacity_ <<= 1ULL) < capacity) {}
    char* new_buffer = static_cast<char*>(malloc(capacity_));
    DoConsume(capacity_);
    char*& big_buffer = big_buffer_;
    if (was_big) {
      memcpy(new_buffer, big_buffer, keep_size);
      free(big_buffer);
      DoConsume(-old_capacity);
    } else {
      memcpy(new_buffer, small_buffer_, keep_size);
    }
    return big_buffer = new_buffer;
  }

  bool big() const {
    return capacity_ > SmallLen;
  }

  char* ptr() {
    return !big() ? small_buffer_ : big_buffer_;
  }

  const char* ptr() const {
    return !big() ? small_buffer_ : big_buffer_;
  }

  size_t capacity_ = SmallLen;
  size_t size_;
  union {
    char small_buffer_[SmallLen];
    char* big_buffer_;
  };
  [[no_unique_address]] std::conditional_t<
      kTrackMemoryUsage, ScopedTrackedConsumption, std::tuple<>> consumption_;
};

template <
    size_t SmallLenLhs, bool kTrackMemoryUsageLhs, size_t SmallLenRhs, bool kTrackMemoryUsageRhs>
bool operator<(
    const ByteBufferBase<SmallLenLhs, kTrackMemoryUsageLhs>& lhs,
    const ByteBufferBase<SmallLenRhs, kTrackMemoryUsageRhs>& rhs) {
  return lhs.AsSlice().compare(rhs.AsSlice()) < 0;
}

template <
    size_t SmallLenLhs, bool kTrackMemoryUsageLhs, size_t SmallLenRhs, bool kTrackMemoryUsageRhs>
bool operator==(
    const ByteBufferBase<SmallLenLhs, kTrackMemoryUsageLhs>& lhs,
    const ByteBufferBase<SmallLenRhs, kTrackMemoryUsageRhs>& rhs) {
  return lhs.AsSlice() == rhs.AsSlice();
}

struct ByteBufferHash {
  typedef std::size_t result_type;

  template <size_t SmallLen, bool kTrackMemoryUsage>
  result_type operator()(const ByteBufferBase<SmallLen, kTrackMemoryUsage>& buffer) const {
    return buffer.AsSlice().hash();
  }
};

template <size_t SmallLen>
using ByteBuffer = ByteBufferBase<SmallLen, false>;

template <size_t SmallLen>
using MemTrackedByteBuffer = ByteBufferBase<SmallLen, true>;

} // namespace yb
