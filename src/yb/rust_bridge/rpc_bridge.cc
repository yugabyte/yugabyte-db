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

#include "yb/rust_bridge/rpc_bridge.h"

#include <cstring>
#include <vector>

#include <google/protobuf/io/coded_stream.h>

#include "yb/rpc/local_call.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_service.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_format.h"

#include "common/yb-cpp-host/src/ffi/rpc.rs.h"

namespace yb::rust_bridge {

namespace {

std::string SerializeAny(const rpc::AnyMessageConstPtr& message) {
  return message.is_lightweight() ? message.lightweight()->SerializeAsString()
                                  : message.protobuf()->SerializeAsString();
}

class RustServiceIf : public rpc::ServiceIf {
 public:
  RustServiceIf(
      std::string wire_name, std::string metric_name, const rust::Vec<rust::String>& methods,
      rust::Box<RustRpcService> service)
      : wire_name_(std::move(wire_name)), metric_name_(std::move(metric_name)),
        service_(std::move(service)) {
    methods_.reserve(methods.size());
    for (const auto& method : methods) {
      methods_.emplace_back(wire_name_, std::string(method));
    }
  }

  void FillEndpoints(const rpc::RpcServicePtr& service, rpc::RpcEndpointMap* map) override {
    for (size_t i = 0; i != methods_.size(); ++i) {
      map->emplace(methods_[i].serialized_body(), std::make_pair(service, i));
    }
  }

  void Handle(rpc::InboundCallPtr call) override {
    const auto index = call->method_index();
    handle_call(*service_, std::make_unique<InboundCallHandle>(std::move(call), index));
  }

  std::string service_name() const override { return wire_name_; }
  std::string metric_name() const override { return metric_name_; }

 private:
  const std::string wire_name_;
  const std::string metric_name_;
  std::vector<rpc::RemoteMethod> methods_;
  rust::Box<RustRpcService> service_;
};

}  // namespace

Status RawMessage::ParseFromCodedStream(google::protobuf::io::CodedInputStream* cis) {
  data_.clear();
  const void* data;
  int size;
  while (cis->GetDirectBufferPointer(&data, &size) && size > 0) {
    data_.append(static_cast<const char*>(data), size);
    cis->Skip(size);
  }
  return Status::OK();
}

void RawMessage::AppendToDebugString(std::string* out) const {
  *out += Format("<$0 serialized bytes>", data_.size());
}

uint8_t* RawMessage::DoSerializeToArray(uint8_t* out) const {
  memcpy(out, data_.data(), data_.size());
  return out + data_.size();
}

void RegisterRustService(
    RpcServerHandle& server, rust::Str wire_name, rust::Str metric_name,
    const rust::Vec<rust::String>& methods, size_t queue_limit, bool high_priority,
    rust::Box<RustRpcService> service) {
  auto service_if = std::make_shared<RustServiceIf>(
      std::string(wire_name), std::string(metric_name), methods, std::move(service));
  auto status = server.RegisterRustService(
      queue_limit, std::move(service_if),
      high_priority ? rpc::ServicePriority::kHigh : rpc::ServicePriority::kNormal);
  if (!status.ok()) {
    throw std::runtime_error(status.ToString());
  }
}

InboundCallHandle::InboundCallHandle(rpc::InboundCallPtr call, size_t method_index)
    : call_(std::move(call)), method_index_(method_index) {
  if (auto* local = dynamic_cast<rpc::LocalYBInboundCall*>(call_.get())) {
    local_request_ = SerializeAny(local->request());
  }
}

InboundCallHandle::~InboundCallHandle() {
  if (!responded_) {
    RespondError(
        static_cast<int32_t>(Status::kInternalError), "Rust handler dropped the call unanswered");
  }
}

rust::Slice<const uint8_t> InboundCallHandle::Request() const {
  Slice request = local_request_ ? Slice(*local_request_) : call_->serialized_request();
  return {request.data(), request.size()};
}

rust::String InboundCallHandle::RemoteAddress() const {
  return yb::ToString(call_->remote_address());
}

int64_t InboundCallHandle::MillisToDeadline() const {
  return ToMilliseconds(call_->GetClientDeadline() - CoarseMonoClock::Now());
}

void InboundCallHandle::RespondSuccess(rust::Slice<const uint8_t> response) {
  responded_ = true;
  auto* yb_call = down_cast<rpc::YBInboundCall*>(call_.get());
  Slice bytes(response.data(), response.size());
  if (auto* local = dynamic_cast<rpc::LocalYBInboundCall*>(yb_call)) {
    // The caller passed its own response object; fill it in place.
    auto response_storage = local->response();
    auto status = response_storage.ParseFromSlice(bytes);
    if (!status.ok()) {
      local->RespondFailure(rpc::ErrorStatusPB::ERROR_APPLICATION, status);
      return;
    }
    local->RespondSuccess(rpc::AnyMessageConstPtr(local->response()));
    return;
  }
  // RespondSuccess serializes the response before returning, so a stack object is enough.
  RawMessage message(bytes.ToBuffer());
  yb_call->RespondSuccess(rpc::AnyMessageConstPtr(&message));
}

void InboundCallHandle::RespondError(int32_t status_code, rust::Str message) {
  responded_ = true;
  call_->RespondFailure(
      rpc::ErrorStatusPB::ERROR_APPLICATION,
      Status(static_cast<Status::Code>(status_code), __FILE__, __LINE__,
             Slice(message.data(), message.size())));
}

void RpcClient::Call(
    rust::Str host, uint16_t port, rust::Str service, rust::Str method,
    rust::Slice<const uint8_t> request, int64_t timeout_ms,
    rust::Box<OutboundCompletion> completion) const {
  struct State {
    rpc::RemoteMethod method;
    RawMessage request;
    RawMessage response;
    rpc::RpcController controller;
    std::optional<rust::Box<OutboundCompletion>> completion;
    rpc::ProxyPtr proxy;
  };
  auto state = std::make_shared<State>();
  state->method = rpc::RemoteMethod(std::string(service), std::string(method));
  state->request = RawMessage(
      std::string(reinterpret_cast<const char*>(request.data()), request.size()));
  state->completion.emplace(std::move(completion));
  state->controller.set_timeout(MonoDelta::FromMilliseconds(timeout_ms));
  state->proxy = proxy_cache_->GetProxy(
      HostPort(std::string(host), port), /* protocol= */ nullptr, MonoDelta());
  state->proxy->AsyncRequest(
      &state->method, /* method_metrics= */ nullptr, state->request, &state->response,
      &state->controller, [state] {
        const auto status = state->controller.status();
        const auto& data = state->response.data();
        complete_outbound(
            std::move(*state->completion), status.ok() ? 0 : static_cast<int32_t>(status.code()),
            status.ok() ? rust::Str() : rust::Str(status.ToString()),
            status.ok() ? rust::Slice<const uint8_t>(
                              reinterpret_cast<const uint8_t*>(data.data()), data.size())
                        : rust::Slice<const uint8_t>());
      });
}

std::unique_ptr<RpcClient> MakeRpcClient(RpcServerHandle& server) {
  return std::make_unique<RpcClient>(&server.GetProxyCache());
}

}  // namespace yb::rust_bridge
