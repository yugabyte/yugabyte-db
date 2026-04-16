// Copyright (c) YugabyteDB, Inc.
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

#include <boost/container/small_vector.hpp>

#include "yb/gutil/casts.h"

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

namespace yb {

class ScopedTrackedConsumption;

constexpr size_t kMinWriteBufferBlocks = 16;

struct WriteBufferPos {
  size_t index;
  char* address;
};

class WriteBuffer {
 public:
  explicit WriteBuffer(size_t block_size, ScopedTrackedConsumption* consumption = nullptr)
      : block_size_(block_size), consumption_(consumption) {}

  WriteBuffer(WriteBuffer&& rhs) : block_size_(rhs.block_size_), consumption_(nullptr) {
    auto consumption = rhs.consumption_;
    rhs.consumption_ = nullptr;
    Take(&rhs);
    consumption_ = consumption;
  }

  void operator=(WriteBuffer&& rhs) {
    Reset();
    Take(&rhs);
  }

  void PushBack(char value);

  void AppendWithPrefix(char prefix, const char* data, size_t len) {
    AppendWithPrefix(prefix, Slice(data, len));
  }

  void AppendWithPrefix(char prefix, const char* data, const char* end) {
    AppendWithPrefix(prefix, data, end - data);
  }

  template <class Value>
  void AppendWithPrefix(char prefix, Value value) {
    DoAppend(prefix, value);
  }

  void Append(const char* data, size_t length) {
    Append(Slice(data, length));
  }

  void Append(const char* data, const char* end) {
    Append(data, end - data);
  }

  template <class Value>
  void Append(Value value) {
    DoAppend(value);
  }

  template <class... Values>
  void AppendValues(Values&&... values) {
    DoAppend(std::forward<Values>(values)...);
  }

  Status Write(const WriteBufferPos& pos, const char* data, const char* end);

  Status Write(const WriteBufferPos& pos, const char* data, size_t length) {
    return Write(pos, data, data + length);
  }

  Status Write(const WriteBufferPos& pos, Slice slice) {
    return Write(pos, slice.cdata(), slice.size());
  }

  void AddBlock(const RefCntBuffer& buffer, size_t skip);
  void Take(WriteBuffer* source);
  void Reset();
  void Flush(boost::container::small_vector_base<RefCntSlice>* output);

  WriteBufferPos Position();
  size_t BytesAfterPosition(const WriteBufferPos& pos) const;
  Status Truncate(const WriteBufferPos& pos);

  bool empty() {
    return blocks_.empty();
  }

  size_t size() const {
    return size_without_last_block_ + filled_in_last_block();
  }

  void AllocateBlock(size_t space);

  char* FirstBlockData() const {
    return blocks_.front().data();
  }

  Slice FirstBlockSlice() const;

  void AppendTo(std::string* out) const;
  void AssignTo(std::string* out) const;
  void AssignTo(size_t begin, size_t end, std::string* out) const;

  void AppendTo(faststring* out) const;
  void AssignTo(faststring* out) const;

  std::string ToBuffer() const;
  std::string ToBuffer(size_t begin, size_t end) const;
  RefCntSlice ExtractContinuousBlock(size_t begin, size_t end) const;

  RefCntSlice ToContinuousBlock() const {
    return ExtractContinuousBlock(0, size());
  }

  void CopyTo(size_t begin, size_t end, std::byte* out) const;

  void CopyTo(std::byte* out) const {
    CopyTo(0, size(), out);
  }

  void Swap(WriteBuffer& rhs);

  template <class F>
  void IterateBlocks(const F& f) const {
    size_t last_block = blocks_.size();
    if (last_block == 0) {
      return;
    }
    --last_block;
    for (size_t i = 0; i != last_block; ++i) {
      f(blocks_[i].AsSlice());
    }
    f(Slice(blocks_[last_block].data(), last_block_free_begin_));
  }

 private:
  void ShrinkLastBlock();

  template <class Out>
  void DoAppendTo(Out* out) const;

  inline static size_t AppendSize(char value) {
    return sizeof(decltype(value));
  }

  template <class Value>
  inline static size_t AppendSize(Value value) {
    return value.size();
  }

  template <class Value, class... Values>
  inline static size_t AppendSize(Value&& value, Values&&... values) {
    return AppendSize(std::forward<Value>(value)) + AppendSize(std::forward<Values>(values)...);
  }

  inline static size_t DoAppendCopyTo(char* out, char value) {
    *out = value;
    return AppendSize(value);
  }

  template <class Value>
  inline static size_t DoAppendCopyTo(char* out, Value&& value) {
    value.CopyTo(out);
    return AppendSize(std::forward<Value>(value));
  }

  template <class Value>
  inline static size_t DoAppendCopyTo(char* out, Value&& value, size_t copy_size) {
    return DoAppendCopyTo(out, value.AsSlice(), copy_size);
  }

  template <class Value, class... Values>
  inline static void AppendCopyTo(char* out, Value&& value, Values&&... values) {
    [[maybe_unused]] const auto copied_size = DoAppendCopyTo(out, std::forward<Value>(value));
    if constexpr (sizeof...(Values)) {
      AppendCopyTo(out + copied_size, std::forward<Values>(values)...);
    }
  }

  template <class... Args>
  void DoAppend(Args&&... value) {
    auto len = AppendSize(std::forward<Args>(value)...);

    auto out = last_block_free_begin_;
    auto end = last_block_free_end_;

    // Use bit_cast to make UBSAN happy. Otherwise, it does not like adding non-zero to nullptr.
    auto new_end = bit_cast<char*>(bit_cast<ptrdiff_t>(out) + len);
    if (PREDICT_TRUE(new_end <= end)) {
      last_block_free_begin_ = new_end;
      AppendCopyTo(out, std::forward<Args>(value)...);
      return;
    }

    DoAppendFallback(out, end - out, std::forward<Args>(value)...);
  }

  template <class... Values>
  void AppendToNewBlock(Values&&... values) {
    auto total_size = AppendSize(std::forward<Values>(values)...);
    auto block_size = std::max(total_size, block_size_);
    AllocateBlock(block_size);
    auto& block = blocks_.back();
    auto* block_start = block.data();
    AppendCopyTo(block_start, std::forward<Values>(values)...);
    last_block_free_begin_ = block_start + total_size;
    last_block_free_end_   = block_start + block_size;
  }

  inline void DoAppendSplit(char* out, size_t out_size, Slice value) {
    // It is definitely known, that the given slice does not fit the given space.
    DoAppendSplitAtValue(out, out_size, value);
  }

  template <class... Values>
  inline void DoAppendSplitAtValue(char* out, size_t out_size, Slice value, Values&&... values) {
    DCHECK_GT(out_size, 0);
    DCHECK_LT(out_size, AppendSize(value));
    memcpy(out, value.data(), out_size);
    AppendToNewBlock(value.WithoutPrefix(out_size), std::forward<Values>(values)...);
  }

  template <class... Values>
  inline void DoAppendSplit(char* out, size_t out_size, char value, Values&&... values) {
    DCHECK_GT(out_size, 0);

    // It is expected char value always fits the given space.
    *(out++) = value;

    // It is required to go via fallback to check the remaining available size, because this
    // current value could be somewhere in the middle of the original list of values.
    DoAppendFallback(out, out_size - 1, std::forward<Values>(values)...);
  }

  template <class... Values>
  inline void DoAppendSplit(char* out, size_t out_size, Slice value, Values&&... values) {
    DCHECK_GT(out_size, 0);

    // If the value does not fit into the given space, split at this value.
    if (out_size < value.size()) {
      return DoAppendSplitAtValue(out, out_size, value, std::forward<Values>(values)...);
    }

    // The value fits into the given space, hence copy the value to the buffer and continue
    // with the remaining values.
    const auto copied = DoAppendCopyTo(out, value);
    DCHECK_EQ(copied, value.size());
    DoAppendFallback(out + copied, out_size - copied, std::forward<Values>(values)...);
  }

  // It is expected a value is either a char, or a Slice, or implements AsSlice().
  template <class Value, class... Values>
  inline void DoAppendSplit(char* out, size_t out_size, Value&& value, Values&&... values) {
    return DoAppendSplit(out, out_size, value.AsSlice(), std::forward<Values>(values)...);
  }

  template <class... Values>
  inline void DoAppendFallback(char* out, size_t out_size, Values&&... values) {
    if (out_size == 0) {
      return AppendToNewBlock(std::forward<Values>(values)...);
    }

    DoAppendSplit(out, out_size, std::forward<Values>(values)...);
  }

  size_t filled_in_last_block() const {
    return last_block_free_begin_ ? last_block_free_begin_ - blocks_.back().data() : 0;
  }

  class Block {
   public:
    explicit Block(size_t size) : buffer_(size), skip_(0) {}
    Block(const RefCntBuffer& buffer, size_t skip) : buffer_(buffer), skip_(skip) {}

    size_t size() const {
      return buffer_.size() - skip_;
    }

    char* data() const {
      return buffer_.data() + skip_;
    }

    char* end() const {
      return buffer_.end();
    }

    void Shrink(size_t size) {
      buffer_.Shrink(size + skip_);
    }

    Slice AsSlice() const {
      return Slice(data(), buffer_.end());
    }

    const RefCntBuffer& buffer() const {
      return buffer_;
    }

    RefCntSlice MoveToRefCntSlice() {
      auto slice = AsSlice();
      return {std::move(buffer_), slice};
    }

   private:
    RefCntBuffer buffer_;
    size_t skip_;
  };

  const size_t block_size_;
  char* last_block_free_begin_ = nullptr;
  char* last_block_free_end_ = nullptr;
  size_t size_without_last_block_ = 0;
  boost::container::small_vector<Block, kMinWriteBufferBlocks> blocks_;
  ScopedTrackedConsumption* consumption_;
};

}  // namespace yb
