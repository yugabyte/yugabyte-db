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

#include "yb/tserver/xcluster_async_executor.h"
#include "yb/rpc/messenger.h"
#include "yb/util/logging.h"
#include "yb/util/source_location.h"
#include "yb/util/threadpool.h"

#define RETURN_WHEN_OFFLINE \
  do { \
    if (IsOffline()) { \
      LOG_WITH_PREFIX(WARNING) << " went offline"; \
      return; \
    } \
  } while (false)

#define LOCK_WEAK_PTR_AND_RETURN_WHEN_OFFLINE \
  auto shared_ptr = weak_ptr.lock(); \
  if (!shared_ptr || shared_ptr->IsOffline()) { \
    return; \
  } \
  do { \
  } while (false)

using namespace std::placeholders;
using namespace std::chrono_literals;

namespace yb {
XClusterAsyncExecutor::XClusterAsyncExecutor(
    ThreadPool* thread_pool, rpc::Messenger* messenger, rpc::Rpcs* rpcs)
    : thread_pool_(thread_pool),
      messenger_(messenger),
      rpcs_(rpcs),
      rpc_handle_(rpcs->InvalidHandle()) {}

XClusterAsyncExecutor::~XClusterAsyncExecutor() {}

void XClusterAsyncExecutor::StartShutdown() {
  CHECK(IsOffline());

  auto reactor_task_id = rpc::kInvalidTaskId;
  {
    std::lock_guard l(reactor_task_id_mutex_);
    reactor_task_id = reactor_task_id_;
  }
  if (reactor_task_id != rpc::kInvalidTaskId) {
    messenger_->AbortOnReactor(reactor_task_id);
  }
}

void XClusterAsyncExecutor::CompleteShutdown() {
  rpc::RpcCommandPtr rpc_to_abort;
  {
    std::lock_guard l(rpc_handle_lock_);
    if (rpc_handle_ != rpcs_->InvalidHandle()) {
      rpc_to_abort = *rpc_handle_;
    }
  }

  if (rpc_to_abort) {
    rpc_to_abort->Abort();
  }
}

void XClusterAsyncExecutor::ScheduleFunc(
    std::string&& func_name, const std::function<void()>&& func) {
  RETURN_WHEN_OFFLINE;

  VLOG_WITH_PREFIX(5) << "Scheduling '" << func_name << "' on thread pool";
  auto s = thread_pool_->SubmitFunc(std::bind(
      &XClusterAsyncExecutor::WeakPtrCallback, weak_from_this(), std::move(func_name),
      std::move(func)));
  if (!s.ok()) {
    MarkFailed("we could not schedule work on thread pool", s);
    return;
  }
  last_task_schedule_time_ = MonoTime::Now();
}

void XClusterAsyncExecutor::WeakPtrCallback(
    const std::weak_ptr<XClusterAsyncExecutor>& weak_ptr, const std::string& func_name,
    const std::function<void()>& func) {
  LOCK_WEAK_PTR_AND_RETURN_WHEN_OFFLINE;
  VLOG(5) << shared_ptr->LogPrefix() << "Executing '" << func_name << "'";

  func();
}

void XClusterAsyncExecutor::RpcCallback(
    const std::weak_ptr<XClusterAsyncExecutor>& weak_ptr, rpc::Rpcs::Handle rpc_handle,
    rpc::Rpcs* rpcs, std::string&& func_name, std::function<void()>&& func) {
  VLOG_WITH_FUNC(5) << "Received rpc callback for " << func_name;

  auto shared_ptr = weak_ptr.lock();
  if (shared_ptr) {
    {
      std::lock_guard l(shared_ptr->rpc_handle_lock_);
      DCHECK(shared_ptr->rpc_handle_ == rpc_handle);
      shared_ptr->rpc_handle_ = rpcs->InvalidHandle();
    }
    shared_ptr->ScheduleFunc(std::move(func_name), std::move(func));
  }

  auto retained_self = rpcs->Unregister(&rpc_handle);
}

void XClusterAsyncExecutor::SetHandleAndSendRpc(const rpc::Rpcs::Handle& rpc_handle) {
  {
    std::lock_guard l(rpc_handle_lock_);
    DCHECK(rpc_handle_ == rpcs_->InvalidHandle());
    rpc_handle_ = rpc_handle;
  }
  (**rpc_handle).SendRpc();
}

void XClusterAsyncExecutor::ScheduleFuncWithDelay(
    int64_t delay_ms, std::string&& func_name, const std::function<void()>&& func) {
  if (delay_ms <= 0) {
    // Execute immediately.
    ScheduleFunc(std::move(func_name), std::move(func));
    return;
  }

  std::lock_guard l(reactor_task_id_mutex_);
  RETURN_WHEN_OFFLINE;

  if (reactor_task_id_ != rpc::kInvalidTaskId) {
    auto msg = Format(
        "we tried to submit work '$0' on reactor while there is already a pending work '$1' ($2)",
        func_name, reactor_task_id_, reactor_task_name_);
    DCHECK(false) << LogPrefix() << " failed: " << msg;
    MarkFailed(msg);
    return;
  }

  VLOG_WITH_PREFIX(5) << "Scheduling '" << func_name << "' on reactor with delay " << delay_ms
                      << "ms";

  auto task_id_result = messenger_->ScheduleOnReactor(
      [weak_ptr = weak_from_this(), func = std::move(func)](const Status& s) {
        ReactorCallback(weak_ptr, s, std::move(func));
      },
      delay_ms * 1ms, SOURCE_LOCATION());

  if (!task_id_result.ok()) {
    MarkFailed("we could not schedule work on reactor", task_id_result.status());
    return;
  }

  ANNOTATE_UNPROTECTED_WRITE(TEST_is_sleeping_) = true;
  reactor_task_id_ = *task_id_result;
  reactor_task_name_ = std::move(func_name);
  VLOG_WITH_PREFIX(5) << "Scheduled task '" << reactor_task_name_ << "' (" << reactor_task_id_
                      << ") on reactor with delay " << delay_ms << "ms";
}

void XClusterAsyncExecutor::ReactorCallback(
    const std::weak_ptr<XClusterAsyncExecutor>& weak_ptr, const Status& status,
    const std::function<void()>&& func) {
  LOCK_WEAK_PTR_AND_RETURN_WHEN_OFFLINE;

  ANNOTATE_UNPROTECTED_WRITE(shared_ptr->TEST_is_sleeping_) = false;
  std::string func_name;
  {
    std::lock_guard l(shared_ptr->reactor_task_id_mutex_);

    DCHECK(shared_ptr->reactor_task_id_ != rpc::kInvalidTaskId);
    VLOG(5) << shared_ptr->LogPrefix() << "Executing '" << shared_ptr->reactor_task_name_ << "' ("
            << shared_ptr->reactor_task_id_ << ") on reactor";
    shared_ptr->reactor_task_id_ = rpc::kInvalidTaskId;
    func_name.swap(shared_ptr->reactor_task_name_);
  }

  if (!status.ok()) {
    shared_ptr->MarkFailed("reactor task failed", status);
    return;
  }

  shared_ptr->ScheduleFunc(std::move(func_name), std::move(func));
}
}  // namespace yb
