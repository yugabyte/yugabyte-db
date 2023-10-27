//
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
//

#include "yb/rpc/local_call.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_header.messages.h"

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
                   callback_thread_pool) {
  TRACE_TO(trace_, "LocalOutboundCall");
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

const std::shared_ptr<LocalYBInboundCall>& LocalOutboundCall::CreateLocalInboundCall() {
  DCHECK(inbound_call_.get() == nullptr);
  const MonoDelta timeout = controller()->timeout();
  const CoarseTimePoint deadline =
      timeout.Initialized() ? start_ + timeout : CoarseTimePoint::max();

  auto outbound_call = std::static_pointer_cast<LocalOutboundCall>(shared_from(this));
  inbound_call_ = InboundCall::Create<LocalYBInboundCall>(
      &rpc_metrics(), remote_method(), outbound_call, deadline);
  return inbound_call_;
}

Result<RefCntSlice> LocalOutboundCall::ExtractSidecar(size_t idx) const {
  return inbound_call_->sidecars().Extract(narrow_cast<int>(idx));
}

size_t LocalOutboundCall::TransferSidecars(Sidecars* dest) {
  return inbound_call_->sidecars().Transfer(dest);
}

LocalYBInboundCall::LocalYBInboundCall(
    RpcMetrics* rpc_metrics,
    const RemoteMethod& remote_method,
    std::weak_ptr<LocalOutboundCall> outbound_call,
    CoarseTimePoint deadline)
    : YBInboundCall(rpc_metrics, remote_method), outbound_call_(outbound_call),
      deadline_(deadline) {
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
}

Status LocalYBInboundCall::ParseParam(RpcCallParams* params) {
  LOG(FATAL) << "local call should not require parsing";
}

Result<size_t> LocalYBInboundCall::ParseRequest(Slice param, const RefCntBuffer& buffer) {
  return STATUS(InternalError, "ParseRequest called for local call");
}

AnyMessageConstPtr LocalYBInboundCall::SerializableResponse() {
  return outbound_call()->response();
}

void LocalYBInboundCallTracker::CallProcessed(InboundCall* call) {
  std::lock_guard<simple_spinlock> lg(lock_);
  calls_being_handled_.erase(call);
}


void LocalYBInboundCallTracker::Enqueue(InboundCall* call, InboundCallWeakPtr call_ptr) {
  std::lock_guard<simple_spinlock> lg(lock_);
  calls_being_handled_.emplace(call, call_ptr);
}

} // namespace rpc
} // namespace yb
