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

#include "yb/rpc/rpc.h"

#include <functional>
#include <string>
#include <thread>

#include "yb/gutil/basictypes.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_header.pb.h"

#include "yb/util/random_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_int64(rpcs_shutdown_timeout_ms, 15000 * yb::kTimeMultiplier,
             "Timeout for a batch of multiple RPCs invoked in parallel to shutdown.");
DEFINE_int64(rpcs_shutdown_extra_delay_ms, 5000 * yb::kTimeMultiplier,
             "Extra allowed time for a single RPC command to complete after its deadline.");
DEFINE_int64(retryable_rpc_single_call_timeout_ms, 5000 * yb::kTimeMultiplier,
             "Timeout of single RPC call in retryable RPC command.");

namespace yb {

using std::shared_ptr;
using strings::Substitute;
using strings::SubstituteAndAppend;

namespace rpc {

bool RpcRetrier::HandleResponse(RpcCommand* rpc, Status* out_status) {
  ignore_result(DCHECK_NOTNULL(rpc));
  ignore_result(DCHECK_NOTNULL(out_status));

  // Always retry a TOO_BUSY error.
  Status controller_status = controller_.status();
  if (controller_status.IsRemoteError()) {
    const ErrorStatusPB* err = controller_.error_response();
    if (err &&
        err->has_code() &&
        err->code() == ErrorStatusPB::ERROR_SERVER_TOO_BUSY) {
      auto status = DelayedRetry(rpc, controller_status);
      if (!status.ok()) {
        *out_status = status;
        return false;
      }
      return true;
    }
  }

  *out_status = controller_status;
  return false;
}

Status RpcRetrier::DelayedRetry(RpcCommand* rpc, const Status& why_status) {
  if (!why_status.ok() && (last_error_.ok() || last_error_.IsTimedOut())) {
    last_error_ = why_status;
  }
  // Add some jitter to the retry delay.
  //
  // If the delay causes us to miss our deadline, RetryCb will fail the
  // RPC on our behalf.
  int num_ms = attempt_num_++ + RandomUniformInt(0, 4);

  RpcRetrierState expected_state = RpcRetrierState::kIdle;
  while (!state_.compare_exchange_strong(expected_state, RpcRetrierState::kWaiting)) {
    if (expected_state == RpcRetrierState::kFinished) {
      auto result = STATUS_FORMAT(IllegalState, "Retry of finished command: $0", rpc);
      LOG(WARNING) << result;
      return result;
    }
    if (expected_state == RpcRetrierState::kWaiting) {
      auto result = STATUS_FORMAT(IllegalState, "Retry of already waiting command: $0", rpc);
      LOG(WARNING) << result;
      return result;
    }
  }
  task_id_ = messenger_->ScheduleOnReactor(
      std::bind(&RpcRetrier::DoRetry, this, rpc, _1), MonoDelta::FromMilliseconds(num_ms));
  return Status::OK();
}

void RpcRetrier::DoRetry(RpcCommand* rpc, const Status& status) {
  auto retain_rpc = rpc->shared_from_this();

  RpcRetrierState expected_state = RpcRetrierState::kWaiting;
  bool run = state_.compare_exchange_strong(expected_state, RpcRetrierState::kRunning);
  if (!run && expected_state == RpcRetrierState::kIdle) {
    run = state_.compare_exchange_strong(expected_state, RpcRetrierState::kRunning);
  }
  task_id_ = -1;
  if (!run) {
    rpc->Finished(STATUS_FORMAT(
        Aborted, "$0 aborted: $1", rpc->ToString(), yb::rpc::ToString(expected_state)));
    return;
  }
  Status new_status = status;
  if (new_status.ok()) {
    // Has this RPC timed out?
    if (deadline_.Initialized()) {
      MonoTime now = MonoTime::Now();
      if (deadline_.ComesBefore(now)) {
        string err_str = Substitute(
          "$0 passed its deadline $1 (now: $2)", rpc->ToString(),
          deadline_.ToString(), now.ToString());
        if (!last_error_.ok()) {
          SubstituteAndAppend(&err_str, ": $0", last_error_.ToString());
        }
        new_status = STATUS(TimedOut, err_str);
      }
    }
  }
  if (new_status.ok()) {
    controller_.Reset();
    rpc->SendRpc();
  } else {
    rpc->Finished(new_status);
  }
  expected_state = RpcRetrierState::kRunning;
  state_.compare_exchange_strong(expected_state, RpcRetrierState::kIdle);
}

RpcRetrier::~RpcRetrier() {
  DCHECK_EQ(-1, task_id_);
}

void RpcRetrier::Abort() {
  RpcRetrierState expected_state = RpcRetrierState::kIdle;
  while (!state_.compare_exchange_weak(expected_state, RpcRetrierState::kFinished)) {
    if (expected_state == RpcRetrierState::kFinished) {
      break;
    }
    if (expected_state != RpcRetrierState::kWaiting) {
      expected_state = RpcRetrierState::kIdle;
    }
    std::this_thread::sleep_for(10ms);
  }
  for (;;) {
    auto task_id = task_id_.load(std::memory_order_acquire);
    if (task_id == -1) {
      break;
    }
    messenger_->AbortOnReactor(task_id);
    std::this_thread::sleep_for(10ms);
  }
}

std::string RpcRetrier::ToString() const {
  return Format("{ task_id: $0 state: $1 deadline: $2 }",
                task_id_.load(std::memory_order_acquire),
                state_.load(std::memory_order_acquire),
                deadline_);
}

RpcController* RpcRetrier::PrepareController() {
  const auto single_call_timeout =
      MonoDelta::FromMilliseconds(FLAGS_retryable_rpc_single_call_timeout_ms);
  controller_.set_deadline(std::min(deadline_, MonoTime::Now() + single_call_timeout));
  return &controller_;
}

Rpcs::Rpcs(std::mutex* mutex) {
  if (mutex) {
    mutex_ = mutex;
  } else {
    mutex_holder_.emplace();
    mutex_ = &mutex_holder_.get();
  }
}

void Rpcs::Shutdown() {
  std::vector<Calls::value_type> calls;
  {
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!shutdown_) {
      shutdown_ = true;
      calls.reserve(calls_.size());
      calls.assign(calls_.begin(), calls_.end());
    }
  }
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(FLAGS_rpcs_shutdown_timeout_ms);
  // It takes some time to complete rpc command after its deadline has passed.
  // So we add extra time for it.
  auto single_call_extra_delay = std::chrono::milliseconds(FLAGS_rpcs_shutdown_extra_delay_ms);
  for (auto& call : calls) {
    CHECK(call);
    call->Abort();
    deadline = std::max(deadline, call->deadline().ToSteadyTimePoint() + single_call_extra_delay);
  }
  {
    std::unique_lock<std::mutex> lock(*mutex_);
    while (!calls_.empty()) {
      LOG(INFO) << "Waiting calls: " << calls_.size();
      if (cond_.wait_until(lock, deadline) == std::cv_status::timeout) {
        break;
      }
    }
    CHECK(calls_.empty()) << "Calls: " << yb::ToString(calls_);
  }
}

void Rpcs::Register(RpcCommandPtr call, Handle* handle) {
  if (*handle == calls_.end()) {
    *handle = Register(std::move(call));
  }
}

Rpcs::Handle Rpcs::Register(RpcCommandPtr call) {
  std::lock_guard<std::mutex> lock(*mutex_);
  if (shutdown_) {
    call->Abort();
    return InvalidHandle();
  }
  calls_.push_back(std::move(call));
  return --calls_.end();
}

void Rpcs::RegisterAndStart(RpcCommandPtr call, Handle* handle) {
  CHECK(*handle == calls_.end());
  Register(std::move(call), handle);
  if (*handle != InvalidHandle()) {
    (***handle).SendRpc();
  }
}

RpcCommandPtr Rpcs::Unregister(Handle* handle) {
  if (*handle == calls_.end()) {
    return RpcCommandPtr();
  }
  auto result = **handle;
  {
    std::lock_guard<std::mutex> lock(*mutex_);
    calls_.erase(*handle);
    cond_.notify_one();
  }
  *handle = calls_.end();
  return result;
}

Rpcs::Handle Rpcs::Prepare() {
  std::lock_guard<std::mutex> lock(*mutex_);
  if (shutdown_) {
    return InvalidHandle();
  }
  calls_.emplace_back();
  return --calls_.end();
}

void Rpcs::Abort(std::initializer_list<Handle*> list) {
  std::vector<RpcCommandPtr> to_abort;
  {
    std::lock_guard<std::mutex> lock(*mutex_);
    for (auto& handle : list) {
      if (*handle != calls_.end()) {
        to_abort.push_back(**handle);
      }
    }
  }
  if (to_abort.empty()) {
    return;
  }
  for (auto& rpc : to_abort) {
    rpc->Abort();
  }
  {
    std::unique_lock<std::mutex> lock(*mutex_);
    for (auto& handle : list) {
      while (*handle != calls_.end()) {
        cond_.wait(lock);
      }
    }
  }
}

} // namespace rpc
} // namespace yb
