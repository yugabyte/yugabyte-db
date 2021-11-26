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

#ifndef YB_RPC_CALL_DATA_H
#define YB_RPC_CALL_DATA_H

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace rpc {

YB_STRONGLY_TYPED_BOOL(ShouldReject);

struct CallData {
 public:
  CallData() : data_(nullptr), size_(0) {}

  explicit CallData(size_t size, ShouldReject should_reject = ShouldReject::kFalse)
      : data_(!should_reject && size ? static_cast<char*>(malloc(size)) : nullptr), size_(size) {}

  CallData(const CallData&) = delete;
  void operator=(const CallData&) = delete;

  CallData(CallData&& rhs) : data_(rhs.data_), size_(rhs.size_) {
    rhs.data_ = nullptr;
    rhs.size_ = 0;
  }

  CallData& operator=(CallData&& rhs) {
    Reset();
    std::swap(data_, rhs.data_);
    std::swap(size_, rhs.size_);
    return *this;
  }

  ~CallData() {
    Reset();
  }

  void Reset() {
    if (data_) {
      free(data_);
    }
    size_ = 0;
    data_ = nullptr;
  }

  bool empty() const {
    return size_ == 0;
  }

  char* data() const {
    return data_;
  }

  bool should_reject() const { return data_ == nullptr; }

  size_t size() const {
    return size_;
  }

  size_t DynamicMemoryUsage() const { return size_; }

 private:
  char* data_;
  size_t size_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_CALL_DATA_H
