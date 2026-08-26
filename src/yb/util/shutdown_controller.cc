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

#include "yb/util/shutdown_controller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/lw_function.h"

namespace yb {

namespace {

using State = ShutdownController::State;

// Advances `state` from `from` to `to`, CHECK-failing if it is not in `from`. Used to move a phase
// to its completed value.
void AdvanceState(std::atomic<State>& state, State from, State to) {
  auto expected_state = from;
  CHECK(state.compare_exchange_strong(expected_state, to));
}

} // namespace

ShutdownController::~ShutdownController() {
  DCHECK(state() == State::kShutdownCompleted);
}

Status ShutdownController::Start() {
  auto expected_state = State::kShutdownCompleted;
  if (state_.compare_exchange_strong(expected_state, State::kRunning)) {
    return Status::OK();
  }

  // Since there's no state before Running, it is expected that Start() could be triggered
  // on the newly-created controller.
  if (expected_state == State::kRunning) {
    return Status::OK();
  }

  return STATUS_FORMAT(ShutdownInProgress, "Shutting down is still in progress");
}

ShutdownController::State ShutdownController::state() const {
  return state_.load(std::memory_order_acquire);
}

bool ShutdownController::IsRunning() const {
  return state() == State::kRunning;
}

Status ShutdownController::Begin(StartShutdownTag*) {
  auto expected_state = State::kRunning;
  if (state_.compare_exchange_strong(expected_state, State::kShutdownRequested)) {
    return Status::OK();
  }
  // A concurrent caller won the race. If it is still starting shutdown, wait until it finishes,
  // i.e. moves the state out of kShutdownRequested, so that a CompleteShutdown() issued right
  // after this call does not observe an in-progress start and fail with IllegalState.
  if (expected_state == State::kShutdownRequested) {
    BusyWait(make_lw_function([this] { return state() != State::kShutdownRequested; }),
             "concurrent shutdown start to finish");
  }
  return STATUS(ShutdownInProgress, "Shutting down already in progress");
}

Status ShutdownController::Begin(CompleteShutdownTag*) {
  auto expected_state = State::kShutdownStarted;
  if (state_.compare_exchange_strong(expected_state, State::kShutdownCompleting)) {
    return Status::OK();
  }

  // Depending on the actual state, a different Status is returned.
  if (expected_state < State::kShutdownStarted) {
    return STATUS(IllegalState, "StartShutdown() must be called first");
  }
  // A concurrent caller won the race. If it is still completing shutdown, wait until it finishes,
  // i.e. the state reaches kShutdownCompleted, so that this caller does not proceed past
  // CompleteShutdown() while the object is still being torn down.
  if (expected_state != State::kShutdownCompleted) {
    BusyWait(make_lw_function([this] { return state() == State::kShutdownCompleted; }),
             "concurrent shutdown to complete");
  }
  return STATUS(ShutdownInProgress, "Shutdown already in progress");
}

void ShutdownController::End(StartShutdownTag*) {
  AdvanceState(state_, State::kShutdownRequested, State::kShutdownStarted);
}

void ShutdownController::End(CompleteShutdownTag*) {
  AdvanceState(state_, State::kShutdownCompleting, State::kShutdownCompleted);
}

} // namespace yb
