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

#include "yb/hnsw/types.h"

#include "yb/util/logging.h"

namespace yb::hnsw {

class BlockWriter {
 public:
  BlockWriter() = default;
  BlockWriter(std::byte* out, std::byte* end) : out_(out), end_(end) {
    DCHECK_LE(out_, end_);
  }

  explicit BlockWriter(DataBlock& block)
      : BlockWriter(block.data(), block.data() + block.size()) {}

  BlockWriter(BlockWriter&& rhs)
      : out_(std::exchange(rhs.out_, nullptr)), end_(std::exchange(rhs.end_, nullptr)) {
  }

  void operator=(BlockWriter&& rhs) {
    DCHECK_EQ(out_, end_);
    out_ = rhs.out_;
    end_ = rhs.end_;
    rhs.out_ = rhs.end_;
  }

  ~BlockWriter() {
    DCHECK_EQ(out_, end_);
  }

  std::byte* out() const {
    return out_;
  }

  std::byte* end() const {
    return end_;
  }

  size_t SpaceLeft() const {
    return end_ - out_;
  }

  BlockWriter Split(size_t size) {
    auto old_end = end_;
    end_ = out_ + size;
    DCHECK_LE(end_, old_end);
    return BlockWriter(end_, old_end);
  }

  std::byte* Prepare(size_t size) {
    auto result = out_;
    out_ += size;
    DCHECK_LE(out_, end_);
    return result;
  }

  template <class Value>
  void Append(Value value) {
    Store<Value, HnswEndian>(out_, value);
    out_ += sizeof(Value);
  }

  template <class... Args>
  void AppendBytes(Args&&... args) {
    Slice slice(std::forward<Args>(args)...);
    memcpy(out_, slice.data(), slice.size());
    out_ += slice.size();
  }

 private:
  std::byte* out_ = nullptr;
  std::byte* end_ = nullptr;
};

} // namespace yb::hnsw
