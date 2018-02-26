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

#include "yb/rpc/growable_buffer.h"

#include <iostream>

#include "yb/gutil/strings/substitute.h"

using strings::Substitute;

namespace yb {
namespace rpc {

GrowableBuffer::GrowableBuffer(size_t initial, size_t limit)
    : buffer_(static_cast<uint8_t*>(malloc(initial))),
      limit_(limit),
      capacity_(initial),
      size_(0) {
}

void GrowableBuffer::DumpTo(std::ostream& out) const {
  out << "size: " << size_ << ", capacity: " << capacity_ << ", limit: " << limit_;
}

void GrowableBuffer::Consume(size_t count) {
  if (count > size_) {
    LOG(DFATAL) << "Consume more bytes than contained: " << size_ << " vs " << count;
  }
  if (count) {
    size_t left = size_ - count;
    if (left) {
      memmove(buffer_.get(), buffer_.get() + count, left);
    }
    size_ = left;
  }
}

void GrowableBuffer::Swap(GrowableBuffer* rhs) {
  DCHECK_EQ(limit_, rhs->limit_);

  buffer_.swap(rhs->buffer_);
  std::swap(capacity_, rhs->capacity_);
  std::swap(size_, rhs->size_);
}

Status GrowableBuffer::Reshape(size_t new_capacity) {
  DCHECK_LE(new_capacity, limit_);
  if (new_capacity != capacity_) {
    auto new_buffer = static_cast<uint8_t *>(realloc(buffer_.get(), new_capacity));
    if (!new_buffer) {
      return STATUS(RuntimeError,
          Substitute("Failed to change buffer size from $0 to $1 bytes", capacity_, new_capacity));
    }
    buffer_.release();
    buffer_.reset(new_buffer);
    capacity_ = new_capacity;
  }
  return Status::OK();
}

Status GrowableBuffer::PrepareRead() {
  if (size_ * 2 > capacity_) {
    const size_t new_capacity = std::min(limit_, capacity_ * 2);
    if (size_ == new_capacity) {
      return STATUS_FORMAT(
          Busy, "Prepare read when buffer already full, size: $0, limit: $1", size_, limit_);
    }
    return Reshape(new_capacity);
  }
  return Status::OK();
}

Status GrowableBuffer::EnsureFreeSpace(size_t len) {
  const size_t expected = size_ + len;
  if (expected > limit_ || expected < size_) {
    return STATUS(RuntimeError,
        Substitute(
            "Failed to ensure capacity for $1 more bytes: $0 (current size) + $1 > $2 (limit)",
            size_,
            len,
            limit_));
  }
  if (expected > capacity_) {
    size_t new_capacity = capacity_ * 2;
    while (new_capacity < expected) {
      new_capacity *= 2;
    }
    return Reshape(std::min(limit_, new_capacity));
  }
  return Status::OK();
}

Status GrowableBuffer::AppendData(const uint8_t* data, size_t len) {
  auto status = EnsureFreeSpace(len);
  if (!status.ok()) {
    return status;
  }
  memcpy(write_position(), data, len);
  size_ += len;

  return Status::OK();
}

void GrowableBuffer::DataAppended(size_t len) {
  if (size_ + len > capacity_) {
    LOG(DFATAL) << "Data appended over capacity: " << size_ << " + " << len << " > " << capacity_;
  }
  size_ += len;
}


std::ostream& operator<<(std::ostream& out, const GrowableBuffer& receiver) {
  receiver.DumpTo(out);
  return out;
}

} // namespace rpc
} // namespace yb
