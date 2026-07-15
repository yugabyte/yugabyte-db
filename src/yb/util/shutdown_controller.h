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
#include <utility>

#include "yb/gutil/macros.h"
#include "yb/util/logging.h"
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
//    from kRunning state. If shutting down has been already started by a concurrent caller, blocks
//    until that caller finishes starting shutdown (state leaves kShutdownRequested) and then
//    returns ShutdownInProgress.
// 2) kShutdownCompleting, kShutdownCompleted - used by CompleteShutdown(), should be triggered
//    from kShutdownStarted state. Returns IllegalState if the state is less than kShutdownStarted.
//    If completion of shutdown has been already triggered by a concurrent caller, blocks until that
//    caller finishes completing shutdown (state reaches kShutdownCompleted) and then returns
//    ShutdownInProgress.
// 3) kRunning - used by Start(), should be triggered from kShutdownCompleted.
//
// Blocking the loser of a phase race until the winner finishes that phase guarantees that a caller
// which drives both phases back-to-back (e.g. StartShutdown() immediately followed by
// CompleteShutdown()) never observes an in-progress start when it completes, and never proceeds
// past CompleteShutdown() while another caller is still tearing the object down.
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

  class StartShutdownTag;
  class CompleteShutdownTag;

  template <class Tag>
  class Scope {
   public:
    // Transfers ownership of the phase to the new object and nulls the source, so exactly one Scope
    // runs the End() callback. Without this a moved-from Scope would invoke End() a second time on
    // destruction.
    Scope(Scope&& rhs) noexcept
        : controller_(rhs.controller_), status_(std::move(rhs.status_)) {
      rhs.controller_ = nullptr;
    }

    // Scope is move-constructible but not move-assignable: nothing reassigns a live Scope, and a
    // defaulted move assignment would fail to run End() on the phase being overwritten.
    Scope& operator=(Scope&&) = delete;

    ~Scope() {
      if (controller_) {
        controller_->End(static_cast<Tag*>(nullptr));
      }
    }

    explicit operator bool() const noexcept {
      return status_.ok();
    }

    [[nodiscard]] const Status& status() const {
      return status_;
    }

   private:
    explicit Scope(ShutdownController& controller)
        : controller_(&controller), status_(controller.Begin(static_cast<Tag*>(nullptr))) {
      if (!status_.ok()) {
        controller_ = nullptr;
      }
    }

    ShutdownController* controller_;
    Status status_;

    friend class ShutdownController;

    DISALLOW_COPY_AND_ASSIGN(Scope);
  };

  ~ShutdownController();

  Status Start();

  [[nodiscard]] Scope<StartShutdownTag> StartShutdown() {
    return Scope<StartShutdownTag>(*this);
  }

  [[nodiscard]] Scope<CompleteShutdownTag> CompleteShutdown() {
    return Scope<CompleteShutdownTag>(*this);
  }

  // Convenience wrappers around StartShutdown() / CompleteShutdown() that DFATAL if the phase
  // could not be entered for an unexpected reason (i.e. anything other than shutdown already
  // being in progress). They let callers collapse the common pattern to:
  //   auto scope = shutdown_controller_.CheckedStartShutdown(LogPrefix());
  //   if (!scope) {
  //     return;
  //   }
  [[nodiscard]] auto CheckedStartShutdown(const std::string& log_prefix = std::string()) {
    return LogIfUnexpected(StartShutdown(), log_prefix);
  }

  [[nodiscard]] auto CheckedCompleteShutdown(const std::string& log_prefix = std::string()) {
    return LogIfUnexpected(CompleteShutdown(), log_prefix);
  }

  [[nodiscard]] State state() const;

  [[nodiscard]] bool IsRunning() const;

 private:
  // Begin/End callbacks driving the StartShutdown() and CompleteShutdown() phases, selected by tag
  // and invoked by the returned Scope on scope entry and exit respectively.
  Status Begin(StartShutdownTag*);
  Status Begin(CompleteShutdownTag*);
  void End(StartShutdownTag*);
  void End(CompleteShutdownTag*);

  // Passes the scope through, DFATAL-ing if it failed for a reason other than shutdown already
  // being in progress.
  template <class ScopeType>
  [[nodiscard]] static ScopeType LogIfUnexpected(ScopeType scope, const std::string& log_prefix) {
    if (!scope) {
      LOG_IF(DFATAL, !scope.status().IsShutdownInProgress()) << log_prefix << scope.status();
    }
    return scope;
  }

  std::atomic<State> state_{ State::kRunning };
};

} // namespace yb
