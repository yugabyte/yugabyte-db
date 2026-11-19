//
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
//

#include "yb/rpc/local_call.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_header.messages.h"

#include "yb/util/dist_trace.h"
#include "yb/util/format.h"
#include "yb/util/memory/memory.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace rpc {

using std::shared_ptr;

LocalOutboundCall::LocalOutboundCall(
    const RemoteMethod& remote_method,
    const shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
    AnyMessagePtr response_storage, RpcController* controller,
    std::shared_ptr<RpcMetrics> rpc_metrics, ResponseCallback callback,
    ThreadPool* callback_thread_pool)
    : OutboundCall(remote_method, outbound_call_metrics, /* method_metrics= */ nullptr,
                   response_storage, controller, std::move(rpc_metrics), std::move(callback),
                   callback_thread_pool, /* metadata_serializer_factory= */ nullptr) {
  TRACE_TO(trace_, "LocalOutboundCall");
  if (otel_span_) {
    otel_span_->SetAttribute("rpc.local_call", true);
  }
}

Status LocalOutboundCall::SetRequestParam(
    AnyMessageConstPtr req, std::unique_ptr<Sidecars> sidecars, const MemTrackerPtr& mem_tracker) {
  req_ = req;
  if (sidecars) {
    return STATUS_FORMAT(NotSupported, "Sidecars not supported for local calls");
  }
  return Status::OK();
}

void LocalOutboundCall::Serialize(ByteBlocks* output) {
  LOG(FATAL) << "Local call should not require serialization";
}

const std::shared_ptr<LocalYBInboundCall>& LocalOutboundCall::CreateLocalInboundCall(
    CallStateListenerFactory* call_state_listener_factory,
    std::shared_ptr<void> buffers_holder) {
  DCHECK(inbound_call_.get() == nullptr);
  const MonoDelta timeout = controller()->timeout();
  const CoarseTimePoint deadline =
      timeout.Initialized() ? start_ + timeout : CoarseTimePoint::max();

  auto outbound_call = std::static_pointer_cast<LocalOutboundCall>(shared_from(this));
  inbound_call_ = InboundCall::Create<LocalYBInboundCall>(
      &rpc_metrics(), remote_method(), outbound_call, req_, response_, controller_->pool_tag(),
      deadline, std::move(buffers_holder), call_state_listener_factory);
  return inbound_call_;
}

Result<RefCntSlice> LocalOutboundCall::ExtractSidecar(size_t idx) const {
  return inbound_call_->sidecars().Extract(narrow_cast<int>(idx));
}

size_t LocalOutboundCall::TransferSidecars(Sidecars* dest) {
  return inbound_call_->sidecars().Transfer(dest);
}

void LocalOutboundCall::TimeOut() {
  DoSetTimedOut(STATUS_FORMAT(
      TimedOut, "Local $0 RPC (request call id $1) timed out after $2",
      remote_method().method_name(), call_id(), MonoDelta(CoarseMonoClock::now() - start_)));
}

LocalYBInboundCall::LocalYBInboundCall(
    RpcMetrics* rpc_metrics,
    const RemoteMethod& remote_method,
    std::weak_ptr<LocalOutboundCall> outbound_call,
    AnyMessageConstPtr request,
    AnyMessagePtr response,
    ThreadPoolTag pool_tag,
    CoarseTimePoint deadline,
    std::shared_ptr<void> buffers_holder,
    CallStateListenerFactory* call_state_listener_factory)
    : YBInboundCall(rpc_metrics, remote_method, call_state_listener_factory),
      outbound_call_(outbound_call),
      request_(request),
      response_(response),
      pool_tag_(pool_tag),
      deadline_(deadline),
      buffers_holder_(std::move(buffers_holder)),
      can_time_out_(buffers_holder_ && deadline != CoarseTimePoint::max()) {
  UpdateWaitStateInfo();
}

const Endpoint& LocalYBInboundCall::remote_address() const {
  static const Endpoint endpoint;
  return endpoint;
}

const Endpoint& LocalYBInboundCall::local_address() const {
  static const Endpoint endpoint;
  return endpoint;
}

void LocalYBInboundCall::Respond(AnyMessageConstPtr resp, bool is_success) {
  // The handler is done with the caller's buffers. Release them before the caller gets the
  // response, so it can reuse them right away.
  buffers_holder_.reset();
  if (completed_.exchange(true, std::memory_order_acq_rel)) {
    // Timed out, the caller does not wait for the response anymore.
    NotifyTransferred(Status::OK(), /* ConnectionPtr */ nullptr);
    return;
  }

  auto call = outbound_call();
  if (!call) {
    LOG(DFATAL) << "Outbound call is NULL during Respond, looks like double response. "
                << "is_success: " << is_success;
    return;
  }

  if (is_success) {
    call->SetFinished();
  } else {
    std::unique_ptr<ErrorStatusPB> error;
    if (resp.is_lightweight()) {
      error = std::make_unique<ErrorStatusPB>();
      yb::down_cast<const LWErrorStatusPB&>(*resp.lightweight()).ToGoogleProtobuf(error.get());
    } else {
      error = std::make_unique<ErrorStatusPB>(
          yb::down_cast<const ErrorStatusPB&>(*resp.protobuf()));
    }
    auto status = STATUS(RemoteError, "Local call error", error->message());
    call->SetFailed(std::move(status), std::move(error));
  }
  NotifyTransferred(Status::OK(), /* ConnectionPtr */ nullptr);
}

void LocalYBInboundCall::UpdateWaitStateInfo() {
  if (auto* listener = call_state_listener()) {
    listener->UpdateInfo(IsLocalCall(), method_name().ToBuffer());
  }
}

Status LocalYBInboundCall::ParseParam(RpcCallParams* params) {
  LOG(FATAL) << "local call should not require parsing";
}

Status LocalYBInboundCall::ParseRequest(Slice param, const RefCntBuffer& buffer) {
  return STATUS(InternalError, "ParseRequest called for local call");
}

AnyMessageConstPtr LocalYBInboundCall::SerializableResponse() {
  return response_;
}

TaggedThreadPools::Tag LocalYBInboundCall::pool_tag() const {
  return pool_tag_;
}

void LocalYBInboundCall::TimeOut() {
  DCHECK(can_time_out_);
  if (completed_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  if (auto call = outbound_call()) {
    call->TimeOut();
  }
}

void LocalYBInboundCallTracker::CallProcessed(InboundCall* call) {
  std::lock_guard lg(lock_);
  CHECK_NOTNULL(call);
  calls_being_handled_.erase(call->instance_id());
  deadlines_.erase({call->GetClientDeadline(), call->instance_id()});
}

void LocalYBInboundCallTracker::Enqueue(const LocalYBInboundCallPtr& call) {
  CHECK_NOTNULL(call)->SetCallProcessedListener(this);
  std::lock_guard lg(lock_);
  calls_being_handled_.emplace(call->instance_id(), call);
  if (call->can_time_out()) {
    const auto deadline = call->GetClientDeadline();
    deadlines_.emplace(deadline, call->instance_id());
    ScheduleTimeoutCheck(deadline);
  }
}

void LocalYBInboundCallTracker::ScheduleTimeoutCheck(CoarseTimePoint deadline) {
  // Checking a bit after the deadline lets the coarse clock reach it too. Otherwise the caller
  // could retry, reusing the buffers that the handler still uses.
  constexpr auto kCheckDelay = 10ms;
  const auto check_time = deadline + kCheckDelay;
  if (check_time >= next_timeout_check_) {
    return;
  }
  if (timeout_check_task_ != kUninitializedScheduledTaskId) {
    scheduler_.Abort(timeout_check_task_);
  }
  next_timeout_check_ = check_time;
  timeout_check_task_ = scheduler_.Schedule(
      [this](ScheduledTaskId task_id, const Status& status) { CheckTimeouts(task_id, status); },
      ToSteady(check_time));
}

void LocalYBInboundCallTracker::CheckTimeouts(ScheduledTaskId task_id, const Status& status) {
  if (!status.ok()) {
    return;
  }
  std::vector<LocalYBInboundCallPtr> expired_calls;
  {
    std::lock_guard lg(lock_);
    if (task_id != timeout_check_task_) {
      // Was rescheduled to an earlier time while this task was starting.
      return;
    }
    timeout_check_task_ = kUninitializedScheduledTaskId;
    next_timeout_check_ = CoarseTimePoint::max();
    const auto now = CoarseMonoClock::now();
    while (!deadlines_.empty() && deadlines_.begin()->first <= now) {
      auto it = calls_being_handled_.find(deadlines_.begin()->second);
      if (it != calls_being_handled_.end()) {
        if (auto call = it->second.lock()) {
          expired_calls.push_back(std::move(call));
        }
      }
      deadlines_.erase(deadlines_.begin());
    }
    if (!deadlines_.empty()) {
      ScheduleTimeoutCheck(deadlines_.begin()->first);
    }
  }
  for (const auto& call : expired_calls) {
    call->TimeOut();
  }
}

Status LocalYBInboundCallTracker::DumpRunningRpcs(
    const DumpRunningRpcsRequestPB& req, DumpRunningRpcsResponsePB* resp) {
  decltype(calls_being_handled_) active_calls;
  {
    std::lock_guard lg(lock_);
    active_calls = calls_being_handled_;
  }
  auto* local_calls = resp->mutable_local_calls();
  local_calls->set_remote_ip("local calls");
  local_calls->set_state(RpcConnectionPB::StateType::RpcConnectionPB_StateType_OPEN);
  local_calls->mutable_calls_in_flight()->Reserve(yb::narrow_cast<int>(active_calls.size()));
  for (const auto& [_, weak_call] : active_calls) {
    if (auto ptr = weak_call.lock()) {
      ptr->DumpPB(req, local_calls->add_calls_in_flight());
    }
  }
  return Status::OK();
}

} // namespace rpc
} // namespace yb
