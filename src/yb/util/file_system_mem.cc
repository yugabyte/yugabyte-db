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

#include "yb/util/file_system_mem.h"

#include "yb/util/malloc.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {

Status InMemoryFileState::Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const {
  if (offset > size_) {
    return STATUS_FORMAT(IOError, "Offset ($0) greater than file size ($1).", offset, size_);
  }
  const uint64_t available = size_ - offset;
  if (n > available) {
    n = available;
  }
  if (n == 0) {
    *result = Slice();
    return Status::OK();
  }

  size_t block = offset / kBlockSize;
  size_t block_offset = offset % kBlockSize;

  if (n <= kBlockSize - block_offset) {
    // The requested bytes are all in the first block.
    *result = Slice(blocks_[block] + block_offset, n);
    return Status::OK();
  }

  size_t bytes_to_copy = n;
  uint8_t* dst = scratch;

  while (bytes_to_copy > 0) {
    size_t avail = kBlockSize - block_offset;
    if (avail > bytes_to_copy) {
      avail = bytes_to_copy;
    }
    memcpy(dst, blocks_[block] + block_offset, avail);

    bytes_to_copy -= avail;
    dst += avail;
    block++;
    block_offset = 0;
  }

  *result = Slice(scratch, n);
  return Status::OK();
}

Status InMemoryFileState::PreAllocate(uint64_t size) {
  std::vector<uint8_t> padding(static_cast<size_t>(size), static_cast<uint8_t>(0));
  // TODO optimize me
  memset(padding.data(), 0, sizeof(uint8_t));
  // Clang analyzer thinks the function below can thrown an exception and cause the "padding"
  // memory to leak.
  Status s = AppendRaw(padding.data(), size);
  size_ -= size;
  return s;
}

Status InMemoryFileState::Append(const Slice& data) {
  return AppendRaw(data.data(), data.size());
}

Status InMemoryFileState::AppendRaw(const uint8_t *src, size_t src_len) {
  while (src_len > 0) {
    size_t avail;
    size_t offset = size_ % kBlockSize;

    if (offset != 0) {
      // There is some room in the last block.
      avail = kBlockSize - offset;
    } else {
      // No room in the last block; push new one.
      blocks_.push_back(new uint8_t[kBlockSize]);
      avail = kBlockSize;
    }

    if (avail > src_len) {
      avail = src_len;
    }
    memcpy(blocks_.back() + offset, src, avail);
    src_len -= avail;
    src += avail;
    size_ += avail;
  }

  return Status::OK();
}

size_t InMemoryFileState::memory_footprint() const {
  size_t size = malloc_usable_size(this);
  if (blocks_.capacity() > 0) {
    size += malloc_usable_size(blocks_.data());
  }
  for (uint8_t* block : blocks_) {
    size += malloc_usable_size(block);
  }
  size += filename_.capacity();
  return size;
}


Status InMemorySequentialFile::Read(size_t n, Slice* result, uint8_t* scratch) {
  Status s = file_->Read(pos_, n, result, scratch);
  if (s.ok()) {
    pos_ += result->size();
  }
  return s;
}

Status InMemorySequentialFile::Skip(uint64_t n) {
  if (pos_ > file_->Size()) {
    return STATUS(IOError, "pos_ > file_->Size()");
  }
  const size_t available = file_->Size() - pos_;
  if (n > available) {
    n = available;
  }
  pos_ += n;
  return Status::OK();
}

Status InMemoryRandomAccessFile::Read(
    uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const {
  return file_->Read(offset, n, result, scratch);
}

Result<uint64_t> InMemoryRandomAccessFile::Size() const {
  return file_->Size();
}

Result<uint64_t> InMemoryRandomAccessFile::INode() const {
  return 0;
}

} // namespace yb
