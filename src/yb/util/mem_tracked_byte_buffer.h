// Copyright (c) YugabyteDB, Inc.
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

#include <cstddef>
#include <string>

#include "yb/util/byte_buffer.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/slice.h"

namespace yb {

class MemTrackerConsumer {
 public:
  MemTrackerConsumer() = default;

  explicit MemTrackerConsumer(const MemTrackerPtr& mem_tracker)
      : consumption_(
            mem_tracker ? ScopedTrackedConsumption(mem_tracker, 0) : ScopedTrackedConsumption()) {}

  MemTrackerConsumer(MemTrackerConsumer&&) = default;
  MemTrackerConsumer& operator=(MemTrackerConsumer&&) = default;

  MemTrackerConsumer(const MemTrackerConsumer& rhs)
      : MemTrackerConsumer(rhs.consumption_.mem_tracker()) {}

  void Add(int64_t delta) {
    if (consumption_) {
      consumption_.Add(delta);
    }
  }

 private:
  ScopedTrackedConsumption consumption_;
};

// ByteBuffer variant whose heap usage is accounted against a MemTracker.
template <size_t SmallLen>
class MemTrackedByteBuffer : public ByteBufferBase<SmallLen, MemTrackerConsumer> {
  using Base = ByteBufferBase<SmallLen, MemTrackerConsumer>;

 public:
  using Base::operator=;

  explicit MemTrackedByteBuffer(const MemTrackerPtr& mem_tracker)
      : Base(MemTrackerConsumer(mem_tracker)) {}

  MemTrackedByteBuffer(const MemTrackerPtr& mem_tracker, const std::string& str)
      : Base(str, MemTrackerConsumer(mem_tracker)) {}

  MemTrackedByteBuffer(const MemTrackerPtr& mem_tracker, Slice slice)
      : Base(slice, MemTrackerConsumer(mem_tracker)) {}

  MemTrackedByteBuffer(const MemTrackerPtr& mem_tracker, Slice slice1, Slice slice2)
      : Base(slice1, slice2, MemTrackerConsumer(mem_tracker)) {}
};

} // namespace yb
