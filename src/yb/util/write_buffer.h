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

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

namespace yb {

class ScopedTrackedConsumption;

constexpr size_t kMinWriteBufferBlocks = 16;

struct WriteBufferPos {
  size_t index;
  size_t offset;
};

class WriteBuffer {
 public:
  explicit WriteBuffer(size_t block_size, ScopedTrackedConsumption* consumption = nullptr)
      : block_size_(block_size), consumption_(consumption) {}

  void Append(const char* data, const char* end);

  void Append(const char* data, size_t length) {
    Append(data, data + length);
  }

  void Append(const Slice& slice) {
    Append(slice.cdata(), slice.cend());
  }

  Status Write(const WriteBufferPos& pos, const char* data, const char* end);

  Status Write(const WriteBufferPos& pos, const char* data, size_t length) {
    return Write(pos, data, data + length);
  }

  Status Write(const WriteBufferPos& pos, const Slice& slice) {
    return Write(pos, slice.cdata(), slice.cend());
  }

  void AddBlock(const RefCntBuffer& buffer, size_t skip);
  void Take(WriteBuffer* source);
  void Reset();
  void Flush(boost::container::small_vector_base<RefCntSlice>* output);

  WriteBufferPos Position() const;
  size_t BytesAfterPosition(const WriteBufferPos& pos) const;

  size_t size() const {
    return size_;
  }

  void AllocateBlock(size_t space);

  char* FirstBlockData() const {
    return blocks_.front().data();
  }

  void AssignTo(std::string* out) const;
  void AssignTo(size_t begin, size_t end, std::string* out) const;
  void CopyTo(size_t begin, size_t end, char* out) const;
  std::string ToBuffer() const;
  std::string ToBuffer(size_t begin, size_t end) const;
  RefCntSlice ExtractContinuousBlock(size_t begin, size_t end) const;

 private:
  const char* CopyToLastBlock(const char* data, const char* end);
  void ShrinkLastBlock();

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
  size_t filled_bytes_in_last_block_ = 0;
  size_t size_ = 0;
  boost::container::small_vector<Block, kMinWriteBufferBlocks> blocks_;
  ScopedTrackedConsumption* consumption_;
};

}  // namespace yb
