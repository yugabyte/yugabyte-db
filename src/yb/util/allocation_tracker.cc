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

#include "yb/util/allocation_tracker.h"

#include "yb/util/logging.h"

#include "yb/util/debug-util.h"

namespace yb {

AllocationTrackerBase::~AllocationTrackerBase() {
#ifndef NDEBUG
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& pair : objects_) {
    LOG(ERROR) << "Error of type " << name_ << " not destroyed, id: " << pair.second.second
               << ", created at: " << pair.second.first;
  }
#else
  if (count_) {
    LOG(ERROR) << "Not all objects of type " << name_ << " were destroyed, "
               << count_ << " objects left";
  }
#endif
}

void AllocationTrackerBase::DoCreated(void* object) {
  LOG(INFO) << "Created " << name_ << ": " << object;
#ifndef NDEBUG
  std::lock_guard<std::mutex> lock(mutex_);
  objects_.emplace(object,
                  std::make_pair(GetStackTrace(StackTraceLineFormat::CLION_CLICKABLE),
                                 ++id_));
#else
  ++count_;
#endif
}

void AllocationTrackerBase::DoDestroyed(void* object) {
  LOG(INFO) << "Destroyed " << name_ << ": " << object;
#ifndef NDEBUG
  std::lock_guard<std::mutex> lock(mutex_);
  objects_.erase(object);
#else
  --count_;
#endif
}

} // namespace yb
