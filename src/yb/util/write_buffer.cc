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

#include "yb/util/write_buffer.h"

#include "yb/gutil/strings/fastmem.h"

#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"

namespace yb {

Status WriteBuffer::Write(const WriteBufferPos& pos, const char* data, const char* end) {
  SCHECK_LT(pos.index, blocks_.size(), InvalidArgument, "Write to out of bounds buffer");
  auto len = std::min<size_t>(blocks_[pos.index].end() - pos.address, end - data);
  memcpy(pos.address, data, len);
  data += len;
  if (data == end) {
    return Status::OK();
  }
  return Write(
      WriteBufferPos {.index = pos.index + 1, .address = blocks_[pos.index + 1].data()}, data, end);
}

void WriteBuffer::PushBack(char value) {
  if (last_block_free_begin_ != last_block_free_end_) {
    *last_block_free_begin_++ = value;
    return;
  }

  AppendToNewBlock(Slice(&value, 1));
}

void WriteBuffer::AddBlock(const RefCntBuffer& buffer, size_t skip) {
  ShrinkLastBlock();
  size_without_last_block_ += filled_in_last_block();
  blocks_.emplace_back(buffer, skip);
  auto block_size = buffer.size() - skip;
  if (consumption_ && *consumption_) {
    consumption_->Add(block_size);
  }
  last_block_free_begin_ = last_block_free_end_ = buffer.end();
}

void WriteBuffer::ShrinkLastBlock() {
  if (blocks_.empty()) {
    return;
  }
  auto& block = blocks_.back();
  auto size = last_block_free_begin_ - block.data();
  if (size) {
    blocks_.back().Shrink(size);
  } else {
    blocks_.pop_back();
    last_block_free_begin_ = last_block_free_end_ = nullptr;
  }
}

void WriteBuffer::Take(WriteBuffer* source) {
  source->ShrinkLastBlock();

  if (source->blocks_.empty()) {
    return;
  }

  auto source_size = source->size();
  ShrinkLastBlock();
  size_without_last_block_ += filled_in_last_block();

  blocks_.reserve(blocks_.size() + source->blocks_.size());
  for (auto& block : source->blocks_) {
    blocks_.push_back(std::move(block));
  }
  size_without_last_block_ += source->size_without_last_block_;
  if (consumption_ && *consumption_) {
    consumption_->Add(source_size);
  }
  last_block_free_begin_ = last_block_free_end_ = blocks_.back().end();

  source->Reset();
}

void WriteBuffer::Reset() {
  if (consumption_ && *consumption_) {
    consumption_->Add(-size_without_last_block_);
  }

  last_block_free_begin_ = last_block_free_end_ = nullptr;
  size_without_last_block_ = 0;
  blocks_.clear();
}

void WriteBuffer::AllocateBlock(size_t size) {
  size_without_last_block_ += blocks_.empty() ? 0 : blocks_.back().size();
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
  out->reserve(out->size() + size());
  auto last = blocks_.size() - 1;
  for (size_t i = 0; i != last; ++i) {
    blocks_[i].AsSlice().AppendTo(out);
  }
  out->append(blocks_[last].data(), last_block_free_begin_ - blocks_[last].data());
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
  CHECK_EQ(str.size(), size());
  return str;
}

std::string WriteBuffer::ToBuffer(size_t begin, size_t end) const {
  std::string str;
  AssignTo(begin, end, &str);
  return str;
}

WriteBufferPos WriteBuffer::Position() {
  if (blocks_.empty()) {
    AllocateBlock(block_size_);
    last_block_free_begin_ = blocks_.front().data();
    last_block_free_end_ = blocks_.front().end();
  }
  return WriteBufferPos {
    .index = blocks_.size() - 1,
    .address = last_block_free_begin_,
  };
}

size_t WriteBuffer::BytesAfterPosition(const WriteBufferPos& pos) const {
  if (pos.address == nullptr) {
    return size();
  }
  size_t last = blocks_.size() - 1;
  size_t result = last_block_free_begin_ - blocks_[last].data();
  for (size_t index = pos.index; index != last; ++index) {
    result += blocks_[index].size();
  }
  result -= pos.address - blocks_[pos.index].data();
  return result;
}

Status WriteBuffer::Truncate(const WriteBufferPos& pos) {
  RSTATUS_DCHECK_LT(
    pos.index, blocks_.size(), InvalidArgument,
    Format("Invalid buffer position: block number is $0, but buffer has $1 blocks",
           pos.index, blocks_.size()));
  RSTATUS_DCHECK(
    pos.address >= blocks_[pos.index].data() && pos.address <= blocks_[pos.index].end(),
    InvalidArgument, "Invalid buffer position: address is outside of the block boundaries");
  RSTATUS_DCHECK(
    pos.index < blocks_.size() - 1 || pos.address <= last_block_free_begin_,
    InvalidArgument, "Invalid buffer position: address is in the block free space");
  if (pos.index < blocks_.size() - 1) {
    auto current_last_block = blocks_.end() - 1;
    auto new_last_block = blocks_.begin() + pos.index;
    for (auto it = new_last_block; it != blocks_.end(); ++it) {
      if (consumption_ && *consumption_ && it != new_last_block) {
        consumption_->Add(-it->size());
      }
      if (it != current_last_block) {
        size_without_last_block_ -= it->size();
      }
    }
    last_block_free_end_ = new_last_block->end();
    blocks_.erase(new_last_block + 1, blocks_.end());
  }
  last_block_free_begin_ = pos.address;
  return Status::OK();
}

Slice WriteBuffer::FirstBlockSlice() const {
  return blocks_.size() > 1 ? blocks_[0].AsSlice()
                            : Slice(blocks_[0].data(), last_block_free_begin_);
}

void WriteBuffer::Swap(WriteBuffer& rhs) {
  std::swap(last_block_free_begin_, rhs.last_block_free_begin_);
  std::swap(last_block_free_end_, rhs.last_block_free_end_);
  std::swap(size_without_last_block_, rhs.size_without_last_block_);
  blocks_.swap(rhs.blocks_);
  std::swap(consumption_, rhs.consumption_);
}

}  // namespace yb
