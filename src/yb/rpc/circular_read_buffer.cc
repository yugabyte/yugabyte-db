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

#include "yb/rpc/circular_read_buffer.h"

#include "yb/util/result.h"
#include "yb/util/tostring.h"

namespace yb {
namespace rpc {

CircularReadBuffer::CircularReadBuffer(size_t capacity, const MemTrackerPtr& parent_tracker)
    : consumption_(MemTracker::FindOrCreateTracker("Receive", parent_tracker, AddToParent::kFalse),
                   capacity),
      buffer_(static_cast<char*>(malloc(capacity))), capacity_(capacity) {
}

bool CircularReadBuffer::Empty() {
  return size_ == 0;
}

void CircularReadBuffer::Reset() {
  buffer_.reset();
}

Result<IoVecs> CircularReadBuffer::PrepareAppend() {
  if (!buffer_) {
    return STATUS(IllegalState, "Read buffer was reset");
  }

  IoVecs result;

  if (!prepend_.empty()) {
    result.push_back(iovec{prepend_.mutable_data(), prepend_.size()});
  }

  size_t end = pos_ + size_;
  if (end < capacity_) {
    result.push_back(iovec{buffer_.get() + end, capacity_ - end});
  }
  size_t start = end <= capacity_ ? 0 : end - capacity_;
  if (pos_ > start) {
    result.push_back(iovec{buffer_.get() + start, pos_ - start});
  }

  if (result.empty()) {
    static Status busy_status = STATUS(Busy, "Circular read buffer is full");
    return busy_status;
  }

  return result;
}

std::string CircularReadBuffer::ToString() const {
  return YB_CLASS_TO_STRING(capacity, pos, size);
}

void CircularReadBuffer::DataAppended(size_t len) {
  if (!prepend_.empty()) {
    size_t prepend_len = std::min(len, prepend_.size());
    prepend_.remove_prefix(prepend_len);
    len -= prepend_len;
  }
  size_ += len;
}

IoVecs CircularReadBuffer::AppendedVecs() {
  IoVecs result;

  size_t end = pos_ + size_;
  if (end <= capacity_) {
    result.push_back(iovec{buffer_.get() + pos_, size_});
  } else {
    result.push_back(iovec{buffer_.get() + pos_, capacity_ - pos_});
    result.push_back(iovec{buffer_.get(), end - capacity_});
  }

  return result;
}

bool CircularReadBuffer::Full() {
  return size_ == capacity_;
}

size_t CircularReadBuffer::DataAvailable() {
  return size_;
}

void CircularReadBuffer::Consume(size_t count, const Slice& prepend) {
  pos_ += count;
  if (pos_ >= capacity_) {
    pos_ -= capacity_;
  }
  size_ -= count;
  if (size_ == 0) {
    pos_ = 0;
  }
  DCHECK(prepend_.empty());
  prepend_ = prepend;
  had_prepend_ = !prepend.empty();
}

bool CircularReadBuffer::ReadyToRead() {
  return prepend_.empty() && (had_prepend_ || !Empty());
}

} // namespace rpc
} // namespace yb
