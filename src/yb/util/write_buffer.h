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

 private:
  void ShrinkLastBlock();
  template <class Out>
  void DoAppendTo(Out* out) const;

  template <class Value>
  static size_t AppendSize(Value value) {
    return value.size();
  }

  template <class Value>
  static void AppendCopyTo(char* out, Value value) {
    value.CopyTo(out);
  }

  template <class Value>
  static size_t AppendSize(char prefix, Value value) {
    return value.size() + 1;
  }

  template <class Value>
  static void AppendCopyTo(char* out, char prefix, Value value) {
    *out++ = prefix;
    value.CopyTo(out);
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

  void AppendToNewBlock(Slice slice);
  void AppendToNewBlock(char prefix, Slice slice);

  void DoAppendSplit(char* out, size_t out_size, Slice slice);
  void DoAppendSplit(char* out, size_t out_size, char ch, Slice slice);

  template <class Value>
  void DoAppendFallback(char* out, size_t out_size, Value value) {
    DoAppendFallback(out, out_size, value.AsSlice());
  }

  template <class Value>
  void DoAppendFallback(char* out, size_t out_size, char ch, Value value) {
    DoAppendFallback(out, out_size, ch, value.AsSlice());
  }

  void DoAppendFallback(char* out, size_t out_size, Slice slice);
  void DoAppendFallback(char* out, size_t out_size, char ch, Slice slice);

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
