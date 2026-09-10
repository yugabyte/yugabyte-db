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

#include "yb/util/abort_source.h"

#include "yb/util/logging.h"

namespace yb {

AbortSource::~AbortSource() {
  LOG_IF(DFATAL, active_.load()) << "AbortSource destroyed with an active scope";
}

void AbortSource::DoAbort(Status status) {
  std::lock_guard lock(mutex_);
  // If there are multiple active scopes, then the first finished scope will clear status to OK.
  LOG_IF(DFATAL, active_.load()) << "AbortSource is already active";
  status_ = std::move(status);
  active_.store(true, std::memory_order_release);
}

Status AbortSource::AbortStatus() const {
  if (!active_.load(std::memory_order_acquire)) {
    return Status::OK();
  }
  std::lock_guard lock(mutex_);
  // The active scope could have been destroyed between the check above and locking the mutex.
  return active_.load(std::memory_order_acquire) ? status_ : Status::OK();
}

} // namespace yb
