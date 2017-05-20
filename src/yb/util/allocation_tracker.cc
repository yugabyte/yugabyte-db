//
// Copyright (c) YugaByte, Inc.
//

#include "yb/util/allocation_tracker.h"

#include <glog/logging.h>

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
