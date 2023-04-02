// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <ev++.h> // NOLINT

#include "yb/gutil/thread_annotations.h"

#include "yb/rpc/reactor_task.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/async_util.h"
#include "yb/util/monotime.h"
#include "yb/util/source_location.h"
#include "yb/util/enums.h"
#include "yb/util/locks.h"

namespace yb {
namespace rpc {

class Messenger;

YB_DEFINE_ENUM(MarkAsDoneResult,
               // Successfully marked as done with this call to MarkAsDone.
               (kSuccess)
               // Task already marked as done by another caller to MarkAsDone.
               (kAlreadyDone)
               // We've switched the done_ flag to true, but the task is not scheduled on a reactor
               // thread and reactor_ is nullptr. Subsequent calls to MarkAsDone will return
               // kAlreadyDone.
               (kNotScheduled))

// A ReactorTask that is scheduled to run at some point in the future.
//
// Semantically it works like RunFunctionTask with a few key differences:
// 1. The user function is called during Abort. Put another way, the user function is _always_
//    invoked, even during reactor shutdown.
// 2. To differentiate between Abort and non-Abort, the user function receives a Status as its first
//    argument.
class DelayedTask : public ReactorTask {
 public:
  DelayedTask(StatusFunctor func, MonoDelta when, int64_t id,
              const SourceLocation& source_location, Messenger* messenger);

  // Schedules the task for running later but doesn't actually run it yet.
  void Run(Reactor* reactor) ON_REACTOR_THREAD override;

  // Could be called from non-reactor thread even before reactor thread shutdown.
  void AbortTask(const Status& abort_status);

  std::string ToString() const override;

  std::string LogPrefix() const {
    return ToString() + ": ";
  }

 private:
  void DoAbort(const Status& abort_status) override;

  // Set done_ to true if not set and return true. If done_ is already set, return false.
  MarkAsDoneResult MarkAsDone() EXCLUDES(mtx_);

  // libev callback for when the registered timer fires.
  void TimerHandler(ev::timer& rwatcher, int revents) ON_REACTOR_THREAD;  // NOLINT

  // Stops the libev timer. Schedules a task to stop the timer on reactor thread if the current
  // thread is not the reactor thread. Can be executed on any thread.
  void StopTimer(const Status& abort_status);

  // ----------------------------------------------------------------------------------------------
  // Fields set in the constructor
  // ----------------------------------------------------------------------------------------------

  // User function to invoke when timer fires or when task is aborted.
  StatusFunctor func_;

  // Delay to apply to this task.
  const MonoDelta when_;

  // This task's id.
  const int64_t id_;

  Messenger& messenger_;

  // ----------------------------------------------------------------------------------------------
  // Fields protected by a mutex
  // ----------------------------------------------------------------------------------------------

  mutable simple_spinlock mtx_;

  // Set to true whenever a Run or Abort methods are called.
  bool done_ GUARDED_BY(mtx_) = false;

  // Link back to registering reactor thread.
  Reactor* reactor_ GUARDED_BY(mtx_) = nullptr;

  // Same as reactor_, but can be accessed without holding mtx_.
  std::atomic<Reactor*> reactor_atomic_{nullptr};

  // ----------------------------------------------------------------------------------------------
  // Fields that are only accessed on the reactor thread
  // ----------------------------------------------------------------------------------------------

  // libev timer. Set when Run() is invoked.
  ev::timer timer_ GUARDED_BY_REACTOR_THREAD;
};

}  // namespace rpc
}  // namespace yb
