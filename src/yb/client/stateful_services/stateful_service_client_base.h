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

#include <memory>

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/util/status.h"

using namespace std::placeholders;

namespace yb {

class HostPort;

namespace client {
class YBClient;

#define STATEFUL_SERVICE_RPC(r, service, method_name) \
  template <typename T> \
  inline Result<stateful_service::BOOST_PP_CAT(method_name, ResponsePB)> method_name( \
      const stateful_service::BOOST_PP_CAT(method_name, RequestPB) & req, \
      const T& deadline_or_timeout) { \
    stateful_service::BOOST_PP_CAT(method_name, ResponsePB) resp; \
    RETURN_NOT_OK(InvokeRpcSync( \
        deadline_or_timeout, \
        [](rpc::ProxyCache* cache, const HostPort& hp) { \
          return new stateful_service::BOOST_PP_CAT(service, ServiceProxy)(cache, hp); \
        }, \
        [&req, resp_ptr = &resp, this](rpc::ProxyBase* proxy, rpc::RpcController* controller) { \
          auto status = static_cast<stateful_service::BOOST_PP_CAT(service, ServiceProxy)*>(proxy) \
                            ->method_name(req, resp_ptr, controller); \
          return ExtractStatus(status, *resp_ptr); \
        })); \
    return resp; \
  }

// Input: service name, csv list of RPC methods.
// Creates functions with these signature:
// Result<[RpcMethod]ResponsePB> [RpcMethod](const CoarseTimePoint&, const [RpcMethod]RequestPB&);
// Result<[RpcMethod]ResponsePB> [RpcMethod](const MonoDelta&, const [RpcMethod]RequestPB&);
#define STATEFUL_SERVICE_RPCS(service, ...) \
  BOOST_PP_SEQ_FOR_EACH(STATEFUL_SERVICE_RPC, service, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

// Input: service_name, service_kind and csv list of rpc methods.
// Creates a service client with constructor of type:
// [service_name]ServiceClient(client::YBClient & yb_client);
#define DEFINE_STATEFUL_SERVICE_CLIENT(service_name, service_kind, ...) \
  class BOOST_PP_CAT(service_name, ServiceClient) : public StatefulServiceClientBase { \
   public: \
    BOOST_PP_CAT(service_name, ServiceClient) \
    (client::YBClient & yb_client) \
        : StatefulServiceClientBase(yb_client, StatefulServiceKind::service_kind) {} \
    STATEFUL_SERVICE_RPCS(service_name, __VA_ARGS__); \
  }

class StatefulServiceClientBase {
 public:
  StatefulServiceClientBase(client::YBClient& yb_client, StatefulServiceKind service_kind);

  virtual ~StatefulServiceClientBase() = default;

  Status InvokeRpcSync(
      const CoarseTimePoint& deadline,
      std::function<rpc::ProxyBase*(rpc::ProxyCache*, const HostPort&)> make_proxy,
      std::function<Status(rpc::ProxyBase*, rpc::RpcController*)> rpc_func);

  Status InvokeRpcSync(
      const MonoDelta& timeout,
      std::function<rpc::ProxyBase*(rpc::ProxyCache*, const HostPort&)> make_proxy,
      std::function<Status(rpc::ProxyBase*, rpc::RpcController*)> rpc_func);

 protected:
  template <class RespClass>
  Status ExtractStatus(const Status& rpc_status, const RespClass& resp) {
    RETURN_NOT_OK(rpc_status);

    if (!resp.has_error()) {
      return Status::OK();
    }

    SCHECK_NE(resp.error().code(), AppStatusPB::NOT_FOUND, NotFound, resp.error().message());

    return StatusFromPB(resp.error());
  }

 private:
  void ResetServiceLocation() EXCLUDES(mutex_);

  Result<std::shared_ptr<rpc::ProxyBase>> GetProxy(
      std::function<rpc::ProxyBase*(rpc::ProxyCache*, const HostPort&)> make_proxy)
      EXCLUDES(mutex_);

 private:
  const StatefulServiceKind service_kind_;
  const std::string service_name_;
  YBClient& yb_client_;

  mutable std::mutex mutex_;
  std::shared_ptr<rpc::ProxyBase> proxy_ GUARDED_BY(mutex_);
};
}  // namespace client
}  // namespace yb
