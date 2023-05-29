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

#include "yb/util/write_buffer.h"

#include "yb/util/mem_tracker.h"

namespace yb {

Status WriteBuffer::Write(const WriteBufferPos& pos, const char* data, const char* end) {
  SCHECK_LT(pos.index, blocks_.size(), InvalidArgument, "Write to out of bounds buffer");
  auto len = std::min<size_t>(blocks_[pos.index].size() - pos.offset, end - data);
  memcpy(blocks_[pos.index].data() + pos.offset, data, len);
  data += len;
  if (data == end) {
    return Status::OK();
  }
  return Write(WriteBufferPos {.index = pos.index + 1, .offset = 0}, data, end);
}

void WriteBuffer::AppendToNewBlock(const char* data, size_t len) {
  AllocateBlock(std::max(len, block_size_));
  memcpy(blocks_.back().data(), data, len);
  filled_bytes_in_last_block_ = len;
}

void WriteBuffer::PushBack(char value) {
  size_ += 1;

  if (PREDICT_FALSE(blocks_.empty())) {
    AppendToNewBlock(&value, 1);
    return;
  }

  auto& last_block = blocks_.back();
  auto filled_bytes_in_last_block = filled_bytes_in_last_block_;
  if (PREDICT_FALSE(last_block.size() == filled_bytes_in_last_block)) {
    AppendToNewBlock(&value, 1);
    return;
  }

  last_block.data()[filled_bytes_in_last_block] = value;
  filled_bytes_in_last_block_ += 1;
}

void WriteBuffer::Append(const char* data, size_t len) {
  size_ += len;

  if (PREDICT_FALSE(blocks_.empty())) {
    AppendToNewBlock(data, len);
    return;
  }

  auto& last_block = blocks_.back();
  auto filled_bytes_in_last_block = filled_bytes_in_last_block_;
  auto left = last_block.size() - filled_bytes_in_last_block;
  if (PREDICT_FALSE(left == 0)) {
    AppendToNewBlock(data, len);
    return;
  }

  auto* out = last_block.data() + filled_bytes_in_last_block;
  if (left >= len) {
    filled_bytes_in_last_block_ += len;
    memcpy(out, data, len);
    return;
  }

  memcpy(out, data, left);
  AppendToNewBlock(data + left, len - left);
}

// len_with_prefix is the total size, i.e. prefix size (1 byte) + data size.
void WriteBuffer::AppendWithPrefixToNewBlock(
    char prefix, const char* data, size_t len_with_prefix) {
  AllocateBlock(std::max(len_with_prefix, block_size_));
  auto& block = blocks_.back();
  *block.data() = prefix;
  filled_bytes_in_last_block_ = len_with_prefix;
  memcpy(block.data() + 1, data, --len_with_prefix);
}

void WriteBuffer::AppendWithPrefix(char prefix, const char* data, size_t len) {
  ++len;
  size_ += len;

  if (PREDICT_FALSE(blocks_.empty())) {
    AppendWithPrefixToNewBlock(prefix, data, len);
    return;
  }

  auto& last_block = blocks_.back();
  auto filled_bytes_in_last_block = filled_bytes_in_last_block_;
  auto left = last_block.size() - filled_bytes_in_last_block;
  if (PREDICT_FALSE(left == 0)) {
    AppendWithPrefixToNewBlock(prefix, data, len);
    return;
  }

  auto* out = last_block.data() + filled_bytes_in_last_block;
  *out++ = prefix;
  if (left >= len) {
    filled_bytes_in_last_block_ += len;
    memcpy(out, data, --len);
    return;
  }

  memcpy(out, data, --left);
  AppendToNewBlock(data + left, --len - left);
}

void WriteBuffer::AddBlock(const RefCntBuffer& buffer, size_t skip) {
  ShrinkLastBlock();
  blocks_.emplace_back(buffer, skip);
  auto block_size = buffer.size() - skip;
  size_ += block_size;
  if (consumption_ && *consumption_) {
    consumption_->Add(block_size);
  }
  filled_bytes_in_last_block_ = block_size;
}

void WriteBuffer::ShrinkLastBlock() {
  if (blocks_.empty()) {
    return;
  }
  if (filled_bytes_in_last_block_) {
    blocks_.back().Shrink(filled_bytes_in_last_block_);
  } else {
    blocks_.pop_back();
  }
}

void WriteBuffer::Take(WriteBuffer* source) {
  source->ShrinkLastBlock();

  if (source->blocks_.empty()) {
    return;
  }

  ShrinkLastBlock();

  blocks_.reserve(blocks_.size() + source->blocks_.size());
  for (auto& block : source->blocks_) {
    blocks_.push_back(std::move(block));
  }
  size_ += source->size_;
  if (consumption_ && *consumption_) {
    consumption_->Add(source->size_);
  }
  filled_bytes_in_last_block_ = source->filled_bytes_in_last_block_;

  source->Reset();
}

void WriteBuffer::Reset() {
  if (consumption_ && *consumption_) {
    consumption_->Add(-size_);
  }

  filled_bytes_in_last_block_ = 0;
  size_ = 0;
  blocks_.clear();
}

void WriteBuffer::AllocateBlock(size_t size) {
  blocks_.emplace_back(size);
  if (consumption_ && *consumption_) {
    consumption_->Add(size);
  }
}

void WriteBuffer::Flush(boost::container::small_vector_base<RefCntSlice>* output) {
  ShrinkLastBlock();
  if (blocks_.empty()) {
    return;
  }
  for (auto& block : blocks_) {
    output->push_back(block.MoveToRefCntSlice());
  }
  blocks_.clear();
}

template <class Block, class Callback>
void EnumerateBlocks(
    const boost::container::small_vector_base<Block>& blocks, size_t begin, size_t left,
    const Callback& callback) {
  if (!left) {
    return;
  }
  size_t idx = 0;
  while (begin > blocks[idx].size()) {
    begin -= blocks[idx].size();
    ++idx;
  }
  while (begin + left > blocks[idx].size()) {
    size_t size = blocks[idx].size() - begin;
    callback(blocks[idx].data() + begin, size, blocks[idx].buffer(), false);
    begin = 0;
    left -= size;
    ++idx;
  }
  callback(blocks[idx].data() + begin, left, blocks[idx].buffer(), true);
}

void WriteBuffer::AssignTo(size_t begin, size_t end, std::string* out) const {
  out->clear();
  size_t left = end - begin;
  if (!left) {
    return;
  }
  out->reserve(left);
  EnumerateBlocks(
      blocks_, begin, left, [out](const char* data, size_t size, const RefCntBuffer&, bool) {
    out->append(data, size);
  });
}

void WriteBuffer::CopyTo(size_t begin, size_t end, std::byte* out) const {
  EnumerateBlocks(blocks_, begin, end - begin,
      [&out](const char* data, size_t size, const RefCntBuffer&, bool) {
    memcpy(out, data, size);
    out += size;
  });
}

RefCntSlice WriteBuffer::ExtractContinuousBlock(size_t begin, size_t end) const {
  size_t full_size = end - begin;
  if (!full_size) {
    return RefCntSlice();
  }
  RefCntSlice result;
  char* out = nullptr;
  EnumerateBlocks(blocks_, begin, full_size,
      [&result, &out, full_size](
          const char* data, size_t size, const RefCntBuffer& buffer, bool last) {
    if (!out) {
      if (last) {
        result = RefCntSlice(buffer, Slice(data, size));
        return;
      }
      RefCntBuffer full_buffer(full_size);
      out = full_buffer.data();
      result = RefCntSlice(std::move(full_buffer));
    }
    memcpy(out, data, size);
    out += size;
  });

  return result;
}

template <class Out>
void WriteBuffer::DoAppendTo(Out* out) const {
  if (blocks_.empty()) {
    return;
  }
  out->reserve(out->size() + size_);
  auto last = blocks_.size() - 1;
  for (size_t i = 0; i != last; ++i) {
    blocks_[i].AsSlice().AppendTo(out);
  }
  out->append(blocks_[last].data(), filled_bytes_in_last_block_);
}

void WriteBuffer::AppendTo(std::string* out) const {
  DoAppendTo(out);
}
void WriteBuffer::AssignTo(std::string* out) const {
  out->clear();
  DoAppendTo(out);
}

void WriteBuffer::AppendTo(faststring* out) const {
  DoAppendTo(out);
}

void WriteBuffer::AssignTo(faststring* out) const {
  DoAppendTo(out);
}

std::string WriteBuffer::ToBuffer() const {
  std::string str;
  AssignTo(&str);
  return str;
}

std::string WriteBuffer::ToBuffer(size_t begin, size_t end) const {
  std::string str;
  AssignTo(begin, end, &str);
  return str;
}

WriteBufferPos WriteBuffer::Position() const {
  if (blocks_.empty()) {
    return WriteBufferPos {
      .index = 0,
      .offset = 0,
    };
  }
  return WriteBufferPos {
    .index = blocks_.size() - 1,
    .offset = filled_bytes_in_last_block_,
  };
}

size_t WriteBuffer::BytesAfterPosition(const WriteBufferPos& pos) const {
  size_t result = filled_bytes_in_last_block_;
  size_t last = blocks_.size() - 1;
  for (size_t index = pos.index; index != last; ++index) {
    result += blocks_[index].size();
  }
  result -= pos.offset;
  return result;
}

Slice WriteBuffer::FirstBlockSlice() const {
  return blocks_.size() > 1 ? blocks_[0].AsSlice()
                            : Slice(blocks_[0].data(), filled_bytes_in_last_block_);
}

}  // namespace yb
