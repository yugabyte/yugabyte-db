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

// Bytes-only RPC bridge between the C++ Messenger and Rust (common/yb-cpp-host/src/ffi/rpc.rs).
// Rust implements services and makes calls on serialized messages; the C++ side neither knows nor
// parses their types.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "rust/cxx.h"

#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/service_if.h"

#include "yb/util/status.h"

namespace yb::rust_bridge {

// Rust types, defined by the bridge.
struct RustRpcService;
struct OutboundCompletion;

// A message carried as its serialized bytes.
class RawMessage : public rpc::LightweightMessage {
 public:
  RawMessage() = default;
  explicit RawMessage(std::string data) : data_(std::move(data)) {}

  Status ParseFromCodedStream(google::protobuf::io::CodedInputStream* cis) override;
  size_t SerializedSize() const override { return data_.size(); }
  void AppendToDebugString(std::string* out) const override;
  void Clear() override { data_.clear(); }

  const std::string& data() const { return data_; }

 private:
  uint8_t* DoSerializeToArray(uint8_t* out) const override;

  std::string data_;
};

// Implemented by each process that hosts Rust services: registers services on its RPC server
// (RegisterService is protected there) and gives access to its proxy cache.
class RpcServerHandle {
 public:
  virtual ~RpcServerHandle() = default;

  virtual Status RegisterRustService(
      size_t queue_limit, rpc::ServiceIfPtr service, rpc::ServicePriority priority) = 0;
  virtual rpc::ProxyCache& GetProxyCache() = 0;
  virtual const scoped_refptr<MetricEntity>& GetMetricEntity() const = 0;
};

// Registers a service whose methods Rust handles. `methods` are in the order Rust uses as method
// indexes. Must be called before the RPC server starts.
void RegisterRustService(
    RpcServerHandle& server, rust::Str wire_name, rust::Str metric_name,
    const rust::Vec<rust::String>& methods, size_t queue_limit, bool high_priority,
    rust::Box<RustRpcService> service);

// An inbound call handed to Rust. It responds exactly once: if Rust drops it without responding,
// the destructor answers with an error, so a caller never waits until its deadline.
class InboundCallHandle {
 public:
  InboundCallHandle(rpc::InboundCallPtr call, size_t method_index);
  ~InboundCallHandle();

  InboundCallHandle(const InboundCallHandle&) = delete;
  void operator=(const InboundCallHandle&) = delete;

  size_t MethodIndex() const { return method_index_; }
  rust::Slice<const uint8_t> Request() const;
  rust::String RemoteAddress() const;
  // Milliseconds until the caller's deadline; negative once it has passed.
  int64_t MillisToDeadline() const;

  void RespondSuccess(rust::Slice<const uint8_t> response);
  void RespondError(int32_t status_code, rust::Str message);

 private:
  rpc::InboundCallPtr call_;
  const size_t method_index_;
  // In-process calls arrive as message objects; this holds their serialized request.
  std::optional<std::string> local_request_;
  bool responded_ = false;
};

// Outbound calls on serialized messages. Thread-safe; completions run on reactor threads.
class RpcClient {
 public:
  explicit RpcClient(rpc::ProxyCache* proxy_cache) : proxy_cache_(proxy_cache) {}

  void Call(
      rust::Str host, uint16_t port, rust::Str service, rust::Str method,
      rust::Slice<const uint8_t> request, int64_t timeout_ms,
      rust::Box<OutboundCompletion> completion) const;

 private:
  rpc::ProxyCache* const proxy_cache_;
};

std::unique_ptr<RpcClient> MakeRpcClient(RpcServerHandle& server);

}  // namespace yb::rust_bridge
