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

#include "yb/util/format.h"
#include "yb/util/memory/memory.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace rpc {

using std::shared_ptr;

LocalOutboundCall::LocalOutboundCall(
    const RemoteMethod* remote_method,
    const shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
    google::protobuf::Message* response_storage, RpcController* controller,
    RpcMetrics* rpc_metrics, ResponseCallback callback)
    : OutboundCall(remote_method, outbound_call_metrics, /* method_metrics= */ nullptr,
                   response_storage, controller, rpc_metrics, std::move(callback),
                   /* callback_thread_pool= */ nullptr) {
  TRACE_TO(trace_, "LocalOutboundCall");
}

Status LocalOutboundCall::SetRequestParam(
    const google::protobuf::Message& req, const MemTrackerPtr& mem_tracker) {
  req_ = &req;
  return Status::OK();
}

void LocalOutboundCall::Serialize(boost::container::small_vector_base<RefCntBuffer>* output) {
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

Result<Slice> LocalOutboundCall::GetSidecar(int idx) const {
  if (idx < 0 || idx >= inbound_call_->sidecars_.size()) {
    return STATUS_FORMAT(InvalidArgument, "Index $0 does not reference a valid sidecar", idx);
  }
  return inbound_call_->sidecars_[idx].as_slice();
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

void LocalYBInboundCall::Respond(const google::protobuf::MessageLite& response, bool is_success) {
  auto call = outbound_call();
  if (!call) {
    LOG(DFATAL) << "Outbound call is NULL during Respond, looks like double response. "
                << "is_success: " << is_success;
    return;
  }

  if (is_success) {
    call->SetFinished();
  } else {
    auto error = std::make_unique<ErrorStatusPB>(yb::down_cast<const ErrorStatusPB&>(response));
    auto status = STATUS(RemoteError, "Local call error", error->message());
    call->SetFailed(std::move(status), std::move(error));
  }
}

Status LocalYBInboundCall::ParseParam(google::protobuf::Message* message) {
  LOG(FATAL) << "local call should not require parsing";
}

} // namespace rpc
} // namespace yb
