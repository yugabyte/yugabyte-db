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

#include "yb/gutil/gscoped_ptr.h"

#include "yb/util/status.h"

#include "yb/util/net/socket.h"

namespace yb {
namespace rpc {

// Convenience buffer for receiving bytes.
// Major features:
//   Limit allocated bytes.
//   Resize depending on used size.
//   Consume read data.
class GrowableBuffer {
 public:
  explicit GrowableBuffer(size_t initial, size_t limit);

  inline bool empty() const { return size_ == 0; }
  inline size_t size() const { return size_; }
  inline const uint8_t* begin() const { return buffer_.get(); }
  inline const uint8_t* end() const { return buffer_.get() + size_; }
  inline size_t capacity_left() const { return capacity_ - size_; }
  inline uint8_t* write_position() { return buffer_.get() + size_; }
  inline size_t limit() const { return limit_; }

  void Swap(GrowableBuffer* rhs);
  // Reset buffer size to zero. Like with std::vector Clean does not deallocate any memory.
  void Clear() { size_ = 0; }
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
  CHECKED_STATUS PrepareRead();

  // Mark next `len` bytes as used.
  void DataAppended(size_t len);

  // Ensures that there are at least `len` of unused bytes.
  CHECKED_STATUS EnsureFreeSpace(size_t len);

  // Copies the given data to the end of the buffer, resizing it up to the configured limit.
  CHECKED_STATUS AppendData(const uint8_t* data, size_t len);
  inline CHECKED_STATUS AppendData(const faststring& input) {
    return AppendData(input.data(), input.size());
  }
  inline CHECKED_STATUS AppendData(const Slice& slice) {
    return AppendData(slice.data(), slice.size());
  }
 private:
  CHECKED_STATUS Reshape(size_t new_capacity);

  // Contained data
  std::unique_ptr<uint8_t, FreeDeleter> buffer_;

  // Max capacity for this buffer
  const size_t limit_;

  // Current capacity, i.e. allocated bytes
  size_t capacity_;

  // Currently used bytes
  size_t size_;
};

std::ostream& operator<<(std::ostream& out, const GrowableBuffer& receiver);

} // namespace rpc
} // namespace yb

#endif // YB_RPC_GROWABLE_BUFFER_H
