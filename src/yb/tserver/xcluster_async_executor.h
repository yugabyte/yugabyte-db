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

#include <string>
#include "yb/rpc/rpc.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/gutil/thread_annotations.h"

#define BIND_FUNCTION_AND_ARGS(func, ...) #func, std::bind(&func, this, ##__VA_ARGS__)

namespace yb {
class ThreadPool;

typedef std::function<void(const Status& status, const tserver::WriteResponsePB& response)>
    RpcCallbackFn;

// Helper class that allows to execute functions asynchronously on the thread pool or with a delay.
// Also has helper functions to handle RPC callbacks.
class XClusterAsyncExecutor : public std::enable_shared_from_this<XClusterAsyncExecutor> {
 public:
  explicit XClusterAsyncExecutor(
      ThreadPool* thread_pool, rpc::Messenger* messenger, rpc::Rpcs* rpcs);
  virtual ~XClusterAsyncExecutor();

  virtual std::string LogPrefix() const = 0;

  bool TEST_Is_Sleeping() const { return ANNOTATE_UNPROTECTED_READ(TEST_is_sleeping_); }

 protected:
  virtual void StartShutdown();
  virtual void CompleteShutdown();
  virtual bool IsOffline() = 0;

  void ScheduleFunc(std::string&& func_name, const std::function<void()>&& func);
  void ScheduleFuncWithDelay(
      int64_t delay_ms, std::string&& func_name, const std::function<void()>&& func);

  virtual void MarkFailed(const std::string& reason, const Status& status = Status::OK()) = 0;
  static void WeakPtrCallback(
      const std::weak_ptr<XClusterAsyncExecutor>& weak_ptr, const std::string& func_name,
      const std::function<void()>& func);
  static void RpcCallback(
      const std::weak_ptr<XClusterAsyncExecutor>& weak_ptr, rpc::Rpcs::Handle rpc_handle,
      rpc::Rpcs* rpcs, std::string&& func_name, std::function<void()>&& func);
  void SetHandleAndSendRpc(const rpc::Rpcs::Handle& rpc_handle);

  bool TEST_is_sleeping_ = false;
  ThreadPool* thread_pool_;
  rpc::Messenger* messenger_;
  rpc::Rpcs* rpcs_;
  std::atomic<MonoTime> last_task_schedule_time_ = MonoTime::Now();

 private:
  static void ReactorCallback(
      const std::weak_ptr<XClusterAsyncExecutor>& weak_ptr, const Status& status,
      const std::function<void()>&& func);
  void HandleRpcCallback(const RpcCallbackFn& func);

  std::mutex reactor_task_id_mutex_;
  rpc::ScheduledTaskId reactor_task_id_ GUARDED_BY(reactor_task_id_mutex_) = rpc::kInvalidTaskId;
  std::string reactor_task_name_ GUARDED_BY(reactor_task_id_mutex_);
  std::mutex rpc_handle_lock_;
  rpc::Rpcs::Handle rpc_handle_ GUARDED_BY(rpc_handle_lock_);
};
}  // namespace yb
