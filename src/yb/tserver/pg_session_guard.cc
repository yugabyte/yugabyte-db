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

#include <concepts>
#include <tuple>
#include <type_traits>

#include "yb/tserver/pg_session_guard.h"

namespace yb::tserver {
namespace {

template<class... Args>
concept IsTryToLock =
    std::same_as<std::tuple<std::try_to_lock_t>, std::tuple<std::decay_t<Args>...>>;

} // namespace

PgSessionCrossThreadGuard::~PgSessionCrossThreadGuard() {
  if (!OwnsLock()) {
    return;
  }
  {
    std::lock_guard lock(state_->mutex_);
    DCHECK(state_->is_cross_thread_locked_);
    state_->before_release_(PgSessionGuardState::IsCrossThreadLock::kTrue);
    state_->is_cross_thread_locked_ = false;
  }
  state_->cond_.notify_one();
}

PgSessionCrossThreadGuard::PgSessionCrossThreadGuard(
    PgSessionGuardStatePtr state, std::unique_lock<std::mutex> lock) {
  if (PREDICT_FALSE(!(state && lock.owns_lock() && !state->is_cross_thread_locked_))) {
    LOG_WITH_FUNC(DFATAL)
        << "Bad arguments: "
        << "state is " << (state ? "not null" : "NULL")
        << "lock is " <<  (lock.owns_lock() ? "owned" : "NOT OWNED");
    return;
  }
  state->is_cross_thread_locked_ = true;
  std::swap(state, state_);
}

PgSessionGuard::PgSessionGuard(PgSessionGuardStatePtr state)
    : PgSessionGuard({}, state) {}

PgSessionGuard::PgSessionGuard(PgSessionGuardStatePtr state, std::try_to_lock_t t)
    : PgSessionGuard({}, state, t) {}

template<class... Args>
PgSessionGuard::PgSessionGuard(Tag, PgSessionGuardStatePtr& state, Args&&...args) {
  if (PREDICT_TRUE(state)) {
    std::unique_lock lock(state->mutex_, std::forward<Args>(args)...);
    if constexpr (IsTryToLock<Args...>) {
      if (!lock.owns_lock() || state->is_cross_thread_locked_) {
        return;
      }
    } else {
      static_assert(sizeof...(Args) == 0);
    }
    std::swap(state_, state);
    std::swap(lock_, lock);
    AcquireSession();
    return;
  }
  LOG_WITH_FUNC(DFATAL) << "Bad arguments: state is null";
}

PgSessionGuard::~PgSessionGuard() {
  if (OwnsLock()) {
    state_->before_release_(PgSessionGuardState::IsCrossThreadLock::kFalse);
  }
}

PgSessionCrossThreadGuard PgSessionGuard::ConvertToCrossThreadGuard() {
  LOG_IF_WITH_FUNC(DFATAL, PREDICT_FALSE(!OwnsLock())) << "Not the owner";
  return {std::move(state_), std::move(lock_)};
}

bool PgSessionGuard::AcquireSession(std::optional<CoarseTimePoint> deadline) {
  DCHECK(lock_.owns_lock());
  auto pred = [this] NO_THREAD_SAFETY_ANALYSIS { return !state_->is_cross_thread_locked_; };
  auto& cond = state_->cond_;
  if (!deadline) {
    cond.wait(lock_, pred);
    return true;
  }
  return cond.wait_until(lock_, *deadline, pred);
}

} // namespace yb::tserver
