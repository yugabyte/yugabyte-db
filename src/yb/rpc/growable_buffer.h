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
#ifndef YB_RPC_GROWABLE_BUFFER_H
#define YB_RPC_GROWABLE_BUFFER_H

#include <iosfwd>
#include <memory>

#include <boost/circular_buffer.hpp>

#include "yb/util/mem_tracker.h"
#include "yb/util/status.h"

#include "yb/util/net/socket.h"

namespace yb {

class MemTracker;

namespace rpc {

// Allocates blocks for GrowableBuffer, shared between multiple GrowableBuffers.
// Each allocated block has fixed size - block_size.
// blocks_limits - max number of blocks that could be allocated in non forced mode.
class GrowableBufferAllocator {
 public:
  GrowableBufferAllocator(size_t block_size, const MemTrackerPtr& mem_tracker);
  ~GrowableBufferAllocator();

  size_t block_size() const;

  // forced - ignore blocks_limit, used when growable buffer does not have at least 2 allocated
  // blocks.
  uint8_t* Allocate(bool forced);
  void Free(uint8_t* buffer);

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

// Used in conjuction with std::unique_ptr to return buffer to allocator.
class GrowableBufferDeleter {
 public:
  GrowableBufferDeleter() : allocator_(nullptr) {}
  explicit GrowableBufferDeleter(GrowableBufferAllocator* allocator) : allocator_(allocator) {}

  void operator()(uint8_t* buffer) const {
    allocator_->Free(buffer);
  }

 private:
  GrowableBufferAllocator* allocator_;
};

// Convenience circular buffer for receiving bytes.
// Major features:
//   Limit allocated bytes.
//   Resize depending on used size.
//   Consume read data.
class GrowableBuffer {
 public:
  explicit GrowableBuffer(GrowableBufferAllocator* allocator, size_t limit);

  inline bool empty() const { return size_ == 0; }
  inline size_t size() const { return size_; }
  inline size_t capacity_left() const { return buffers_.size() * block_size_ - size_ - pos_; }
  inline size_t limit() const { return limit_; }

  bool full() const { return pos_ + size_ >= limit_; }

  void Swap(GrowableBuffer* rhs);
  // Reset buffer size to zero. Like with std::vector Clean does not deallocate any memory.
  void Clear() { pos_ = 0; size_ = 0; }
  void DumpTo(std::ostream& out) const;

  // Removes first `count` bytes from buffer, moves remaining bytes to the beginning of the buffer.
  // This function should be used with care, because it has linear complexity in terms of the
  // remaining number of bytes.
  //
  // A good use case for this class and function is the following.
  // This function is used after we parse all complete packets to move incomplete packet to the
  // beginning of the buffer. Usually, there is just a small amount of incomplete data.
  // Since even a big packet is received by parts, we will move only the first received block.
  void Consume(size_t count);

  // Ensures there is some space to read into. Depending on currently used size.
  // Returns iov's that could be used for receiving data into to this buffer.
  Result<IoVecs> PrepareAppend();

  // Returns currently appended data.
  IoVecs AppendedVecs();

  // Mark next `len` bytes as used.
  void DataAppended(size_t len);

 private:
  IoVecs IoVecsForRange(size_t begin, size_t end);

  typedef std::unique_ptr<uint8_t, GrowableBufferDeleter> BufferPtr;

  GrowableBufferAllocator& allocator_;

  const size_t block_size_;

  ScopedTrackedConsumption consumption_;

  // Max capacity for this buffer
  const size_t limit_;

  // Contained data
  boost::circular_buffer<BufferPtr> buffers_;

  // Current start position of used bytes.
  size_t pos_ = 0;

  // Currently used bytes
  size_t size_ = 0;
};

std::ostream& operator<<(std::ostream& out, const GrowableBuffer& receiver);

} // namespace rpc
} // namespace yb

#endif // YB_RPC_GROWABLE_BUFFER_H
