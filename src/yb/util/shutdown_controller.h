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

#include "yb/util/concepts.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"

namespace yb {

// The class is used to control two phase shutdown flow used widely in the code.
// Passes via the following states:
// | Initial |            Starting shutdown            |           Completing shutdown           |
// kRunning => kShutdownRequested => kShutdownStarted => kShutdownCompleting => kShutdownCompleted
//     ^---------------------------------- Start() -------------------------------------'
//
// Each phase passes via two states to be able to guarantee the stage is triggered only one time:
// 0) kRunning - the inital state set during instantiation.
// 1) kShutdownRequested, kShutdownStarted - used by StartShutdown(), should be triggered
//    from kRunning state. Returns ShutdownInProgress if shutting down has been already started.
// 2) kShutdownCompleting, kShutdownCompleted - used by CompleteShutdown(), should be triggered
//    from kShutdownStarted state. Returns IllegalState is the state is less than kShutdownStarted,
//    returns ShutdownInProgress if completion of shutdown has been already triggered.
// 3) kRunning - used by Start(), should be triggered from kShutdownCompleted.
//
// Each method returns a Scope object which handles the state change on leaving the scope.
//
class ShutdownController {
 public:
  enum class State {
    kRunning = 0,
    kShutdownRequested,
    kShutdownStarted,
    kShutdownCompleting,
    kShutdownCompleted
  };

  template <InvocableAs<Status()> Begin, InvocableAs<void()> End>
  class Scope {
   public:
    ~Scope() = default;
    Scope(Scope&&) = default;
    Scope& operator=(Scope&&) = default;

    explicit operator bool() const noexcept {
      return status_.ok();
    }

    [[nodiscard]] const Status& status() const {
      return status_;
    }

   private:
    Scope(Begin&& begin, End&& end) : status_(begin()), scope_exit_(std::move(end)) {
      if (!status_.ok()) {
        scope_exit_.Cancel();
      }
    }

    Status status_;
    CancelableScopeExit<End> scope_exit_;

    friend class ShutdownController;

    DISALLOW_COPY_AND_ASSIGN(Scope);
  };

  ~ShutdownController() {
    DCHECK(state() == State::kShutdownCompleted);
  }

  Status Start() {
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

  [[nodiscard]] auto StartShutdown() {
    auto shutting_down_begin = [this] {
      auto expected_state = State::kRunning;
      return state_.compare_exchange_strong(expected_state, State::kShutdownRequested) ?
          Status::OK() : STATUS(ShutdownInProgress, "Shutting down already in progress");
    };
    auto shutting_down_end = [this] {
      auto expected_state = State::kShutdownRequested;
      CHECK(state_.compare_exchange_strong(expected_state, State::kShutdownStarted));
    };
    return Scope{ std::move(shutting_down_begin), std::move(shutting_down_end) };
  }

  [[nodiscard]] auto CompleteShutdown() {
    auto complete_shutdown_begin = [this] -> Status {
      auto expected_state = State::kShutdownStarted;
      if (state_.compare_exchange_strong(expected_state, State::kShutdownCompleting)) {
        return Status::OK();
      }

      // Depending on the actual state, a different Status is returned.
      if (expected_state < State::kShutdownStarted) {
        return STATUS(IllegalState, "StartShutdown() must be triggered firstly");
      } else {
        return STATUS(ShutdownInProgress, "Shutdown already in progress");
      }
    };
    auto complete_shutdown_end = [this] {
      auto expected_state = State::kShutdownCompleting;
      CHECK(state_.compare_exchange_strong(expected_state, State::kShutdownCompleted));
    };
    return Scope{ std::move(complete_shutdown_begin), std::move(complete_shutdown_end) };
  }

  [[nodiscard]] State state() const {
    return state_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool IsRunning() const {
    return state() == State::kRunning;
  }

 private:
  std::atomic<State> state_{ State::kRunning };
};

} // namespace yb
