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
      capacity_(initial) {
}

void GrowableBuffer::DumpTo(std::ostream& out) const {
  out << "size: " << size_ << ", capacity: " << capacity_ << ", limit: " << limit_;
}

void GrowableBuffer::Consume(size_t count) {
  if (count > size_) {
    LOG(DFATAL) << "Consume more bytes than contained: " << size_ << " vs " << count;
  }
  if (count) {
    pos_ = (pos_ + count) % capacity_;
    size_ -= count;
  }
}

void GrowableBuffer::Swap(GrowableBuffer* rhs) {
  DCHECK_EQ(limit_, rhs->limit_);

  buffer_.swap(rhs->buffer_);
  std::swap(capacity_, rhs->capacity_);
  std::swap(size_, rhs->size_);
  std::swap(pos_, rhs->pos_);
}

Status GrowableBuffer::Reshape(size_t new_capacity) {
  DCHECK_LE(new_capacity, limit_);
  if (new_capacity != capacity_) {
    std::unique_ptr<uint8_t, FreeDeleter> new_buffer(static_cast<uint8_t*>(malloc(new_capacity)));
    if (!new_buffer) {
      return STATUS_FORMAT(RuntimeError,
                           "Failed to change buffer size from $0 to $1 bytes",
                           capacity_, new_capacity);
    }

    auto vecs = AppendedVecs();
    auto* out = new_buffer.get();
    for (const auto& vec : vecs) {
      memcpy(out, vec.iov_base, vec.iov_len);
      out += vec.iov_len;
    }

    buffer_ = std::move(new_buffer);
    pos_ = 0;
    capacity_ = new_capacity;
  }
  return Status::OK();
}

// Returns currently read data.
IoVecs GrowableBuffer::AppendedVecs() {
  IoVecs result;
  auto end = pos_ + size_;
  if (end <= capacity_) {
    result.push_back({ buffer_.get() + pos_, size_ });
  } else {
    result.push_back({ buffer_.get() + pos_, capacity_ - pos_ });
    result.push_back({ buffer_.get(), end - capacity_ });
  }
  return result;
}

Result<IoVecs> GrowableBuffer::PrepareAppend() {
  if (size_ * 2 > capacity_) {
    const size_t new_capacity = std::min(limit_, capacity_ * 2);
    if (size_ == new_capacity) {
      return STATUS_FORMAT(
          Busy, "Prepare read when buffer already full, size: $0, limit: $1", size_, limit_);
    }
    RETURN_NOT_OK(Reshape(new_capacity));
  }

  IoVecs result;
  auto begin = pos_ + size_;
  if (begin < capacity_) {
    result.push_back({ buffer_.get() + begin, capacity_ - begin });
    if (pos_ > 0) {
      result.push_back({ buffer_.get(), pos_ });
    }
  } else {
    begin -= capacity_;
    result.push_back({ buffer_.get() + begin, pos_ - begin });
  }

  return result;
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
  auto begin = pos_ + size_;
  size_ += len;
  if (begin < capacity_) {
    size_t size = std::min(capacity_ - begin, len);
    memcpy(buffer_.get() + begin, data, size);
    data += size;
    len -= size;
    begin = 0;
  } else {
    begin -= capacity_;
  }
  if (len) {
    memcpy(buffer_.get() + begin, data, len);
  }

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
