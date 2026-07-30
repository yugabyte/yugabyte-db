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

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/logging.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::tserver {

struct PgSessionGuardState {
  YB_STRONGLY_TYPED_BOOL(IsCrossThreadLock);
  using BeforeReleaseLockFunctor = std::function<void(IsCrossThreadLock)>;

  PgSessionGuardState(std::mutex& mutex, BeforeReleaseLockFunctor&& before_release_lock)
      : mutex_{mutex}, before_release_{std::move(before_release_lock)} {};

 private:
  friend class PgSessionCrossThreadGuard;
  friend class PgSessionGuard;

  std::mutex& mutex_;
  const BeforeReleaseLockFunctor before_release_;
  std::condition_variable cond_{};
  bool is_cross_thread_locked_ GUARDED_BY(mutex_) {false};

  DISALLOW_COPY_AND_ASSIGN(PgSessionGuardState);
};

using PgSessionGuardStatePtr = std::shared_ptr<PgSessionGuardState>;

class PgSessionCrossThreadGuard {
 public:
  PgSessionCrossThreadGuard(PgSessionCrossThreadGuard&& rhs) = default;

  ~PgSessionCrossThreadGuard();

  [[nodiscard]] bool OwnsLock() const { return state_ != nullptr; }

 private:
  friend class PgSessionGuard;

  PgSessionCrossThreadGuard(PgSessionGuardStatePtr state, std::unique_lock<std::mutex> lock);

  PgSessionGuardStatePtr state_;
};

class PgSessionGuard {
 public:
  class LostOwnership {
   public:
    LostOwnership(PgSessionGuardStatePtr&& state, std::unique_lock<std::mutex>&& lock)
        : state_{std::move(state)}, lock_{std::move(lock)} {}

   private:
    PgSessionGuardStatePtr state_;
    std::unique_lock<std::mutex> lock_;

    DISALLOW_COPY_AND_ASSIGN(LostOwnership);
  };

  explicit PgSessionGuard(PgSessionGuardStatePtr state);

  PgSessionGuard(PgSessionGuard&&) = default;

  ~PgSessionGuard();

  [[nodiscard]] PgSessionCrossThreadGuard ConvertToCrossThreadGuard();

  template<class Pred>
  [[nodiscard]] std::pair<bool, std::optional<LostOwnership>> WaitUntil(
      std::condition_variable& cond, CoarseTimePoint deadline, const Pred& pred) {
    auto result = false;
    if (PREDICT_FALSE(!OwnsLock())) {
      LOG_WITH_FUNC(DFATAL) << "Not the owner";
    } else {
      result = cond.wait_until(lock_, deadline, pred);
      if (!AcquireSession(deadline)) {
        return {std::piecewise_construct,
                std::forward_as_tuple(result),
                std::forward_as_tuple(std::in_place, std::move(state_), std::move(lock_))};
      }
    }
    return {result, std::nullopt};
  }

  [[nodiscard]] bool OwnsLock() const { return state_ != nullptr; }

 private:
  bool AcquireSession(std::optional<CoarseTimePoint> deadline = std::nullopt);

  PgSessionGuardStatePtr state_;
  std::unique_lock<std::mutex> lock_;

  DISALLOW_COPY_AND_ASSIGN(PgSessionGuard);
};

} // namespace yb::tserver
