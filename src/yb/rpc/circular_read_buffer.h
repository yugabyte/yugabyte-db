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

#include "yb/rpc/stream.h"

#include "yb/util/mem_tracker.h"

namespace yb {
namespace rpc {

struct FreeMemory {
  void operator()(void* data) const {
    free(data);
  }
};

// StreamReadBuffer implementation that is based on circular buffer of fixed capacity.
class CircularReadBuffer : public StreamReadBuffer {
 public:
  explicit CircularReadBuffer(size_t capacity, const MemTrackerPtr& parent_tracker);

  bool ReadyToRead() override;
  bool Empty() override;
  void Reset() override;
  Result<IoVecs> PrepareAppend() override;
  std::string ToString() const override;
  void DataAppended(size_t len) override;
  IoVecs AppendedVecs() override;
  bool Full() override;
  void Consume(size_t count, const Slice& prepend) override;
  size_t DataAvailable() override;

 private:
  ScopedTrackedConsumption consumption_;
  std::unique_ptr<char, FreeMemory> buffer_;
  const size_t capacity_;
  size_t pos_ = 0;
  size_t size_ = 0;
  Slice prepend_;
  bool had_prepend_ = false;
};

} // namespace rpc
} // namespace yb
