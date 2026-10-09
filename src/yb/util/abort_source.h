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

#include <atomic>
#include <mutex>

#include "yb/gutil/macros.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/util/scope_exit.h"
#include "yb/util/status.h"

namespace yb {

// Level-triggered abort signal between long-running operations and the code that needs them to
// abandon work, e.g. draining pending tablet reads before RocksDB is destroyed or replaced.
//
// An aborter calls Abort() and keeps the returned scope alive while operations should abort.
// Operations poll AbortStatus(), which is non-OK while the scope is alive. Only one scope may be
// active at a time, so aborters must be serialized by the caller.
// Aborters can be serialized by holding a ScopedRWOperationPause for the lifetime of the abort
// scope.
class AbortSource {
 public:
  AbortSource() = default;

  ~AbortSource();

  // Raises the abort signal with the specified status until the returned scope is destroyed.
  [[nodiscard]] auto Abort(Status status) {
    DoAbort(std::move(status));
    return ScopeExit([this] { active_.store(false, std::memory_order_release); });
  }

  // Returns OK when no abort scope is active, otherwise the status of the active one.
  Status AbortStatus() const;

 private:
  void DoAbort(Status status);

  std::atomic<bool> active_{false};
  mutable std::mutex mutex_;
  Status status_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(AbortSource);
};

} // namespace yb
