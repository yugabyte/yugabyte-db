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

#include <stdint.h>

#include <functional>
#include <thread>

#include <boost/lockfree/stack.hpp>
#include "yb/util/logging.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

using namespace std::placeholders;

namespace yb::rpc {

class GrowableBufferAllocator::Impl : public GarbageCollector,
                                      public std::enable_shared_from_this<Impl> {
 public:
  Impl(size_t block_size,
       const MemTrackerPtr& mem_tracker)
      : block_size_(block_size),
        mandatory_tracker_(MemTracker::FindOrCreateTracker(
            "Mandatory", mem_tracker, AddToParent::kFalse)),
        used_tracker_(MemTracker::FindOrCreateTracker("Used", mem_tracker)),
        allocated_tracker_(MemTracker::FindOrCreateTracker("Allocated", mem_tracker)),
        pool_(0) {
  }

  void CompleteInit() {
    allocated_tracker_->parent()->AddGarbageCollector(shared_from_this());
  }

  ~Impl() override {
    CollectGarbage(std::numeric_limits<size_t>::max());
  }

  uint8_t* Allocate(bool forced) {
    uint8_t* result = nullptr;

    if (forced) {
      if (pool_.pop(result)) {
        allocated_tracker_->Release(block_size_);
      } else {
        result = static_cast<uint8_t*>(malloc(block_size_));
      }
      mandatory_tracker_->Consume(block_size_);
      return result;
    }

    if (!pool_.pop(result)) {
      if (!allocated_tracker_->TryConsume(block_size_)) {
        return nullptr;
      }
      result = static_cast<uint8_t*>(malloc(block_size_));
    }
    used_tracker_->Consume(block_size_);
    allocated_tracker_->Release(block_size_);
    return result;
  }

  void Free(uint8_t* buffer, bool was_forced) {
    if (!buffer) {
      return;
    }

    auto* tracker = was_forced ? mandatory_tracker_.get() : used_tracker_.get();
    tracker->Release(block_size_);
    if (allocated_tracker_->TryConsume(block_size_)) {
      if (!pool_.push(buffer)) {
        allocated_tracker_->Release(block_size_);
        free(buffer);
      }
    } else {
      free(buffer);
    }
  }

  size_t block_size() const {
    return block_size_;
  }

 private:
  void CollectGarbage(size_t required) override {
    uint8_t* buffer = nullptr;
    size_t total = 0;
    while (total < required && pool_.pop(buffer)) {
      free(buffer);
      total += block_size_;
    }
    allocated_tracker_->Release(total);
  }

  const size_t block_size_;
  // Buffers that allocated with force flag, does not could in parent.
  MemTrackerPtr mandatory_tracker_;
  // Buffers that are in use by client of this class.
  MemTrackerPtr used_tracker_;
  // Buffers that is contained in pool.
  MemTrackerPtr allocated_tracker_;
  boost::lockfree::stack<uint8_t*> pool_;
};

GrowableBufferAllocator::GrowableBufferAllocator(
    size_t block_size, const MemTrackerPtr& mem_tracker)
    : impl_(std::make_shared<Impl>(block_size, mem_tracker)) {
  impl_->CompleteInit();
}

GrowableBufferAllocator::~GrowableBufferAllocator() {
}

size_t GrowableBufferAllocator::block_size() const {
  return impl_->block_size();
}

uint8_t* GrowableBufferAllocator::Allocate(bool forced) {
  return impl_->Allocate(forced);
}

void GrowableBufferAllocator::Free(uint8_t* buffer, bool was_forced) {
  impl_->Free(buffer, was_forced);
}

namespace {

constexpr size_t kDefaultBuffersCapacity = 8;

}

GrowableBuffer::GrowableBuffer(GrowableBufferAllocator* allocator, size_t limit)
    : allocator_(*allocator),
      block_size_(allocator->block_size()),
      limit_(limit),
      buffers_(kDefaultBuffersCapacity) {
  buffers_.push_back(
      BufferPtr(allocator_.Allocate(true), GrowableBufferDeleter(&allocator_, true)));
}

std::string GrowableBuffer::ToString() const {
  return YB_CLASS_TO_STRING(size, limit);
}

void GrowableBuffer::Consume(size_t count, const Slice& prepend) {
  if (count > size_) {
    LOG(DFATAL) << "Consume more bytes than contained: " << size_ << " vs " << count;
  }
  if (!prepend.empty()) {
    LOG(DFATAL) << "GrowableBuffer does not support prepending";
  }
  if (count) {
    pos_ += count;
    while (pos_ >= block_size_) {
      pos_ -= block_size_;
      auto buffer = std::move(buffers_.front());
      buffers_.pop_front();
      // Reuse buffer if only one left.
      if (buffers_.size() < 2) {
        buffers_.push_back(std::move(buffer));
      }
    }
    size_ -= count;
    if (size_ == 0) { // Buffer was fully read, so we could reset start position also.
      pos_ = 0;
    }
  }
}

void GrowableBuffer::Swap(GrowableBuffer* rhs) {
  DCHECK_EQ(limit_, rhs->limit_);
  DCHECK_EQ(block_size_, rhs->block_size_);
  DCHECK_EQ(&allocator_, &rhs->allocator_);

  buffers_.swap(rhs->buffers_);
  consumption_.Swap(&rhs->consumption_);
  std::swap(size_, rhs->size_);
  std::swap(pos_, rhs->pos_);
}

IoVecs GrowableBuffer::IoVecsForRange(size_t begin, size_t end) {
  DCHECK_LE(begin, end);

  auto it = buffers_.begin();
  while (begin >= block_size_) {
    ++it;
    begin -= block_size_;
    end -= block_size_;
  }
  IoVecs result;
  while (end > block_size_) {
    result.push_back({ it->get() + begin, block_size_ - begin });
    begin = 0;
    end -= block_size_;
    ++it;
  }
  result.push_back({ it->get() + begin, end - begin });
  return result;
}

// Returns currently read data.
IoVecs GrowableBuffer::AppendedVecs() {
  return IoVecsForRange(pos_, pos_ + size_);
}

Result<IoVecs> GrowableBuffer::PrepareAppend() {
  if (!valid()) {
    return STATUS(IllegalState, "Read buffer was reset");
  }

  DCHECK_LT(pos_, block_size_);

  // Check if we have too small capacity left.
  if (pos_ + size_ * 2 >= block_size_ && capacity_left() * 2 < block_size_) {
    if (buffers_.size() == buffers_.capacity()) {
      buffers_.set_capacity(buffers_.capacity() * 2);
    }
    BufferPtr new_buffer;
    if (buffers_.size() * block_size_ < limit_) {
      // We need at least 2 buffers for normal functioning.
      // Because with one buffer we could reach situation when our command limit is just several
      // bytes.
      bool forced = buffers_.size() < 2;
      new_buffer = BufferPtr(
          allocator_.Allocate(forced), GrowableBufferDeleter(&allocator_, forced));
    }
    if (new_buffer) {
      buffers_.push_back(std::move(new_buffer));
    } else if (capacity_left() == 0) {
      return STATUS_FORMAT(
          Busy, "Prepare read when buffer already full, size: $0, limit: $1", size_, limit_);
    }
  }

  return IoVecsForRange(pos_ + size_, buffers_.size() * block_size_);
}

void GrowableBuffer::DataAppended(size_t len) {
  if (len > capacity_left()) {
    LOG(DFATAL) << "Data appended over capacity: " << len << " > " << capacity_left();
  }
  size_ += len;
}

void GrowableBuffer::Reset() {
  Clear();
  buffers_.clear();
  buffers_.set_capacity(0);
}

bool GrowableBuffer::valid() const {
  return !buffers_.empty();
}

std::ostream& operator<<(std::ostream& out, const GrowableBuffer& receiver) {
  return out << receiver.ToString();
}

} // namespace yb::rpc
