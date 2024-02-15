// Copyright (c) YugaByte, Inc.
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

#include "yb/rpc/poller.h"

#include "yb/rpc/scheduler.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/unique_lock.h"

using namespace std::placeholders;

namespace yb {
namespace rpc {

Poller::Poller(const std::string& log_prefix, std::function<void()> callback)
    : log_prefix_(log_prefix), callback_(callback),
      poll_task_id_(rpc::kUninitializedScheduledTaskId) {
}

void Poller::Start(Scheduler* scheduler, MonoDelta interval) {
  std::lock_guard lock(mutex_);
  if (closing_) {
    return;
  }
  scheduler_ = scheduler;
  interval_ = interval;
  Schedule();
}

void Poller::Shutdown() {
  UniqueLock lock(mutex_);
  if (!closing_) {
    closing_ = true;
    if (scheduler_ == nullptr) {
      // Never started
      return;
    }
    if (poll_task_id_ != rpc::kUninitializedScheduledTaskId) {
      scheduler_->Abort(poll_task_id_);
    }
  }
  WaitOnConditionVariable(&cond_, &lock, [this]() NO_THREAD_SAFETY_ANALYSIS {
    return poll_task_id_ == rpc::kUninitializedScheduledTaskId;
  });
}

void Poller::Schedule() {
  poll_task_id_ = scheduler_->Schedule(
      std::bind(&Poller::Poll, this, _1), interval_.ToSteadyDuration());
}

void Poller::Poll(const Status& status) {
  {
    std::lock_guard lock(mutex_);
    if (!status.ok() || closing_) {
      LOG_WITH_PREFIX(INFO) << "Poll stopped: " << status;
      poll_task_id_ = rpc::kUninitializedScheduledTaskId;
      YB_PROFILE(cond_.notify_one());
      return;
    }
  }

  callback_();

  {
    std::lock_guard lock(mutex_);
    if (!closing_) {
      Schedule();
    } else {
      poll_task_id_ = rpc::kUninitializedScheduledTaskId;
    }
    if (poll_task_id_ == rpc::kUninitializedScheduledTaskId) {
      YB_PROFILE(cond_.notify_one());
    }
  }
}

} // namespace rpc
} // namespace yb
