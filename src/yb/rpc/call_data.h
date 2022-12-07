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

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace rpc {

struct CallData {
 public:
  CallData() : buffer_(EmptyBuffer()) {}

  explicit CallData(size_t size) : buffer_(size) {}
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
    return buffer_.data();
  }

  bool should_reject() const { return !buffer_; }

  void Reset() {
    buffer_.Reset();
  }

  size_t size() const {
    return buffer_.size();
  }

  const RefCntBuffer& buffer() const {
    return buffer_;
  }

  size_t DynamicMemoryUsage() const { return buffer_.DynamicMemoryUsage(); }

 private:
  static RefCntBuffer EmptyBuffer() {
    static RefCntBuffer result(0);
    return result;
  }

  RefCntBuffer buffer_;
};

} // namespace rpc
} // namespace yb
