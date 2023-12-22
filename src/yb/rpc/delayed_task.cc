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
// Portions Copyright (c) Yugabyte, Inc.
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

#include "yb/rpc/delayed_task.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"

#include "yb/util/logging.h"
#include "yb/util/tostring.h"

namespace yb {
namespace rpc {

DelayedTask::DelayedTask(StatusFunctor func, MonoDelta when, int64_t id,
                         const SourceLocation& source_location, Messenger* messenger)
    : ReactorTask(source_location),
      func_(std::move(func)),
      when_(when),
      id_(id),
      messenger_(*messenger) {
}

void DelayedTask::Run(Reactor* reactor) {
  const auto reactor_state = reactor->state();
  if (reactor_state != ReactorState::kRunning) {
    LOG(WARNING) << "Reactor " << reactor->name() << " is not running (state: " << reactor_state
                 << "), not scheduling a delayed task.";
    return;
  }

  // Acquire lock to prevent task from being aborted in the middle of scheduling. In case abort
  // is be requested in the middle of scheduling, task will be aborted right after return from this
  // function.
  std::lock_guard l(mtx_);

  CHECK(reactor_ == nullptr) << "Task has already been scheduled on reactor " << reactor_->name()
                             << ", but now trying to schedule it on reactor " << reactor->name();

  VLOG_WITH_PREFIX_AND_FUNC(4) << "Done: " << done_ << ", when: " << when_;

  if (done_) {
    // Task has been aborted.
    return;
  }

  // Schedule the task to run later.
  reactor_ = reactor;
  reactor_atomic_.store(reactor, std::memory_order_release);
  timer_.set(reactor->loop_);

  // timer_ is owned by this task and will be stopped through AbortTask/Abort before this task
  // is removed from list of scheduled tasks, so it is safe for timer_ to remember pointer to task.
  timer_.set<DelayedTask, &DelayedTask::TimerHandler>(this);

  timer_.start(when_.ToSeconds() /* after */, 0 /* repeat*/);
  reactor_->scheduled_tasks_.insert(shared_from(this));
}

MarkAsDoneResult DelayedTask::MarkAsDone() {
  std::lock_guard l(mtx_);
  if (done_) {
    return MarkAsDoneResult::kAlreadyDone;
  }
  done_ = true;

  // ENG-2879: we need to check if reactor_ is nullptr, because that would mean that the task has
  // not even started.  AbortTask uses the return value of this function to check if it needs to
  // stop the timer, and that is only possible / necessary if Run has been called and reactor_ is
  // not nullptr.
  return reactor_ == nullptr ? MarkAsDoneResult::kNotScheduled
                             : MarkAsDoneResult::kSuccess;
}

std::string DelayedTask::ToString() const {
  return YB_CLASS_TO_STRING(id, source_location);
}

void DelayedTask::AbortTask(const Status& abort_status) {
  auto mark_as_done_result = MarkAsDone();

  VLOG_WITH_PREFIX_AND_FUNC(4)
      << "Status: " << abort_status << ", " << AsString(mark_as_done_result);

  if (mark_as_done_result == MarkAsDoneResult::kSuccess) {
    StopTimer(abort_status);
  }
  if (mark_as_done_result != MarkAsDoneResult::kAlreadyDone) {
    // We need to call the callback whenever we successfully switch the done_ flag to true, whether
    // or not the task has been scheduled.
    func_(abort_status);
    // Clear the function to remove all captured resources.
    func_.clear();
  }
}

void DelayedTask::DoAbort(const Status& abort_status) {
  messenger_.RemoveScheduledTask(id_);
  AbortTask(abort_status);
}

void DelayedTask::TimerHandler(ev::timer& watcher, int revents) {
  auto* reactor = reactor_atomic_.load(std::memory_order_acquire);

  auto mark_as_done_result = MarkAsDone();
  if (mark_as_done_result != MarkAsDoneResult::kSuccess) {
    LOG_IF_WITH_PREFIX(DFATAL, mark_as_done_result != MarkAsDoneResult::kAlreadyDone)
        << "Unexpected state: " << mark_as_done_result << " (expected kAlreadyDone): "
        << "the timer handler is already being called";
    return;
  }

  // Hold shared_ptr, so this task wouldn't be destroyed upon removal below until func_ is called.
  auto holder = shared_from(this);

  reactor->scheduled_tasks_.erase(holder);
  messenger_.RemoveScheduledTask(id_);

  if (EV_ERROR & revents) {
    std::string msg = "Delayed task got an error in its timer handler";
    LOG(WARNING) << msg;
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Abort";
    func_(STATUS(Aborted, msg));
  } else {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Execute";
    func_(Status::OK());
  }
  // Clear the function to remove all captured resources.
  func_.clear();
}

void DelayedTask::StopTimer(const Status& abort_status) {
  auto* reactor = reactor_atomic_.load(std::memory_order_acquire);
  // Stop the libev timer. We don't need to do this in the kNotScheduled case, because the timer
  // has not started in that case.
  if (reactor->IsCurrentThread()) {
    ReactorThreadRoleGuard guard;
    timer_.stop();
  } else {
    // Must call timer_.stop() on the reactor thread. Keep a refcount to prevent this DelayedTask
    // from being deleted. If the reactor thread has already been shut down, this will be a no-op.
    auto scheduling_status =
        reactor->ScheduleReactorFunctor([this, holder = shared_from(this)](Reactor* reactor) {
          ReactorThreadRoleGuard guard;
          timer_.stop();
        }, SOURCE_LOCATION());
    LOG_IF(DFATAL, !scheduling_status.ok())
        << "Could not schedule a reactor task to stop the libev timer in " << __PRETTY_FUNCTION__
        << ": "
        << "scheduling_status=" << scheduling_status
        << ", abort_status=" << abort_status;
  }
}

}  // namespace rpc
}  // namespace yb
