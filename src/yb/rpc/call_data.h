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

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/memory/memory_usage.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace rpc {

struct CallData {
 public:
  CallData() : buffer_(EmptyBuffer()) {}

  explicit CallData(size_t size) : buffer_(RefCntBuffer(size)) {}
  explicit CallData(RefCntSlice slice) : buffer_(std::move(slice)) {}

  class ShouldRejectTag {};

  CallData(size_t size, ShouldRejectTag) {}

  CallData(const CallData&) = delete;
  void operator=(const CallData&) = delete;

  CallData(CallData&& rhs) = default;
  CallData& operator=(CallData&& rhs) = default;

  bool empty() const {
    return buffer_.empty();
  }

  char* data() const {
    return const_cast<char*>(buffer_.data());
  }

  bool should_reject() const { return !buffer_; }

  void Reset() {
    buffer_.Reset();
  }

  size_t size() const {
    return buffer_.size();
  }

  Slice AsSlice() const {
    return buffer_.AsSlice();
  }

  const RefCntBuffer& holder() const {
    return buffer_.holder();
  }

  size_t DynamicMemoryUsage() const { return buffer_.DynamicMemoryUsage(); }

 private:
  static RefCntSlice EmptyBuffer() {
    static RefCntBuffer result(0);
    return RefCntSlice(result);
  }

  RefCntSlice buffer_;
};

class ReceivedSidecars {
 public:
  Result<RefCntSlice> Extract(const RefCntBuffer& buffer, size_t idx) const;

  Result<SidecarHolder> GetHolder(const RefCntBuffer& buffer, size_t idx) const;

  size_t Transfer(const RefCntBuffer& buffer, Sidecars* dest);

  Status Parse(Slice message, const boost::iterator_range<const uint32_t*>& offsets);

  size_t DynamicMemoryUsage() const {
    return GetFlatDynamicMemoryUsageOf(sidecar_bounds_);
  }

  size_t GetCount() const {
    return sidecar_bounds_.size() > 0 ? sidecar_bounds_.size() - 1 : 0;
  }

 private:
  static constexpr size_t kMinBufferForSidecarSlices = 16;
  // Slices of data for rpc sidecars. They point into memory owned by transfer_.
  // Number of sidecars should be obtained from header_.
  boost::container::small_vector<const uint8_t*, kMinBufferForSidecarSlices> sidecar_bounds_;
};

} // namespace rpc
} // namespace yb
