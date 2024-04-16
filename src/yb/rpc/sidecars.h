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

#pragma once

#include <google/protobuf/repeated_field.h>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/locks.h"
#include "yb/util/write_buffer.h"

namespace yb::rpc {

class Sidecars {
 public:
  explicit Sidecars(ScopedTrackedConsumption* consumption = nullptr);

  // Start new sidecar, returning WriteBuffer to fill sidecar to.
  WriteBuffer& Start();

  // Complete started sidecar, returning sidecar index.
  size_t Complete();

  // Take sidecars from specified buffer, with specified offsets in this buffer.
  // Returns index of the first taken sidecar.
  size_t Take(
      WriteBuffer* sidecar_buffer, google::protobuf::RepeatedField<uint32_t>* offsets);

  // Take sidecars from specified buffer, with specified bounds in this buffer.
  // Returns index of the first taken sidecar.
  size_t Take(
      const RefCntBuffer& buffer,
      const boost::container::small_vector_base<const uint8_t*>& sidecar_bounds);

  Slice GetFirst() const;

  RefCntSlice Extract(size_t index) const;

  // Removes all sidecars.
  void Reset();

  Status AssignTo(int idx, std::string* out);

  size_t Transfer(Sidecars* dest);

  void MoveOffsetsTo(size_t body_size, google::protobuf::RepeatedField<uint32_t>* dest);

  size_t size() const {
    return buffer_.size();
  }

  void CopyTo(std::byte* out);

  void Flush(ByteBlocks* output);

  const google::protobuf::RepeatedField<uint32_t>& offsets() const {
    return offsets_;
  }

  WriteBuffer& buffer() {
    return buffer_;
  }

 private:
  simple_spinlock take_mutex_;
  WriteBuffer buffer_;
  google::protobuf::RepeatedField<uint32_t> offsets_;
};

} // namespace yb::rpc
