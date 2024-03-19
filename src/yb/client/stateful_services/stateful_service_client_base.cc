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

#include "yb/client/stateful_services/stateful_service_client_base.h"

#include <chrono>

#include "yb/master/master_client.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/tserver/tablet_server.h"
#include "yb/client/client-internal.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"

DECLARE_bool(TEST_running_test);
DECLARE_string(certs_dir);

DEFINE_RUNTIME_int32(stateful_service_operation_timeout_sec, 120,
    "The number of seconds after which stateful service operations should timeout.");

namespace yb {

using namespace std::chrono_literals;
namespace client {

StatefulServiceClientBase::StatefulServiceClientBase(StatefulServiceKind service_kind)
    : service_kind_(service_kind), service_name_(StatefulServiceKind_Name(service_kind)) {}

StatefulServiceClientBase::~StatefulServiceClientBase() { Shutdown(); }

Status StatefulServiceClientBase::Init(
    const std::string& local_hosts, const std::vector<std::vector<HostPort>>& masters,
    const std::string& root_dir) {
  std::vector<std::string> addresses;
  for (const auto& address : masters) {
    for (const auto& host_port : address) {
      addresses.push_back(host_port.ToString());
    }
  }
  SCHECK(!addresses.empty(), InvalidArgument, "No master address found to StatefulServiceClient.");

  const auto master_addresses = JoinStrings(addresses, ",");

  std::lock_guard lock(mutex_);
  rpc::MessengerBuilder messenger_builder(service_name_ + "_Client");
  secure_context_ =
      VERIFY_RESULT(rpc::SetupInternalSecureContext(local_hosts, root_dir, &messenger_builder));

  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  if (PREDICT_FALSE(FLAGS_TEST_running_test)) {
    std::vector<HostPort> host_ports;
    RETURN_NOT_OK(HostPort::ParseStrings(local_hosts, 0 /* default_port */, &host_ports));
    messenger_->TEST_SetOutboundIpBase(VERIFY_RESULT(HostToAddress(host_ports[0].host())));
  }

  master_client_ = VERIFY_RESULT(
      yb::client::YBClientBuilder()
          .add_master_server_addr(master_addresses)
          .default_admin_operation_timeout(FLAGS_stateful_service_operation_timeout_sec * 1s)
          .Build(messenger_.get()));

  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

  return Status::OK();
}

Status StatefulServiceClientBase::TEST_Init(
    const std::string& local_host, const std::string& master_addresses) {
  std::lock_guard lock(mutex_);
  rpc::MessengerBuilder messenger_builder(service_name_ + "Client");
  secure_context_ = VERIFY_RESULT(rpc::SetupSecureContext(
      FLAGS_certs_dir, local_host, rpc::SecureContextType::kInternal, &messenger_builder));

  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  if (PREDICT_FALSE(FLAGS_TEST_running_test)) {
    messenger_->TEST_SetOutboundIpBase(VERIFY_RESULT(HostToAddress(local_host)));
  }

  master_client_ = VERIFY_RESULT(
      yb::client::YBClientBuilder()
          .add_master_server_addr(master_addresses)
          .default_admin_operation_timeout(FLAGS_stateful_service_operation_timeout_sec * 1s)
          .Build(messenger_.get()));

  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

  return Status::OK();
}

namespace {
bool IsRetryableStatus(const Status& status) {
  return status.IsTryAgain() || status.IsNetworkError() || status.IsServiceUnavailable();
}

const HostPortPB* GetHostPort(master::StatefulServiceInfoPB* info) {
  if (!info->private_rpc_addresses().empty()) {
    return info->mutable_private_rpc_addresses(0);
  }
  if (!info->broadcast_addresses().empty()) {
    return info->mutable_broadcast_addresses(0);
  }
  return nullptr;
}
}  // namespace

Status StatefulServiceClientBase::InvokeRpcSync(
    const CoarseTimePoint& deadline,
    std::function<rpc::ProxyBase*(rpc::ProxyCache*, const HostPort&)>
        make_proxy,
    std::function<Status(rpc::ProxyBase*, rpc::RpcController*)>
        rpc_func) {
  CoarseBackoffWaiter waiter(deadline, 1s /* max_wait */, 100ms /* base_delay */);

  uint64 attempts = 0;
  bool first_run = true;
  while (true) {
    ++attempts;
    if (!first_run) {
      SCHECK(
          waiter.Wait(), TimedOut, "RPC call to $0 timed out after $1 attempt(s)", service_name_,
          waiter.attempt());
    }
    first_run = false;

    auto proxy_result = GetProxy(make_proxy);
    if (!proxy_result.ok()) {
      if (IsRetryableStatus(proxy_result.status())) {
        // Try again with wait.
        VLOG(1) << "Retrying Proxy creation for: " << service_name_
                << " Error: " << proxy_result.status();
        continue;
      }
      return proxy_result.status();
    }

    rpc::RpcController rpc;
    rpc.set_deadline(deadline);

    auto status = rpc_func(proxy_result->get(), &rpc);
    if (!status.ok()) {
      if (status.IsTryAgain()) {
        VLOG(1) << "Retrying RPC call to " << service_name_ << ": " << status;
        continue;
      }

      const auto* rpc_error = rpc.error_response();
      if (IsRetryableStatus(status) ||
          (rpc_error && (rpc_error->code() == rpc::ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN ||
                         rpc_error->code() == rpc::ErrorStatusPB::ERROR_NO_SUCH_SERVICE))) {
        VLOG(1) << "Retrying RPC call to " << service_name_ << " with re-resolve: " << status
                << (rpc_error ? Format(" Rpc error: $0", rpc_error->code()) : "");
        ResetServiceLocation();
        continue;
      }
    }

    TEST_SYNC_POINT_CALLBACK("StatefulServiceClientBase::InvokeRpcSync", &attempts);
    return status;
  }
}

Status StatefulServiceClientBase::InvokeRpcSync(
    const MonoDelta& timeout,
    std::function<rpc::ProxyBase*(rpc::ProxyCache*, const HostPort&)> make_proxy,
    std::function<Status(rpc::ProxyBase*, rpc::RpcController*)> rpc_func) {
return InvokeRpcSync(
    CoarseMonoClock::Now() + timeout, std::move(make_proxy), std::move(rpc_func));
}

void StatefulServiceClientBase::ResetServiceLocation() {
  std::lock_guard lock(mutex_);
  service_hp_.reset();
  proxy_.reset();
}

Result<std::shared_ptr<rpc::ProxyBase>> StatefulServiceClientBase::GetProxy(
    std::function<rpc::ProxyBase*(rpc::ProxyCache*, const HostPort&)> make_proxy) {
  std::lock_guard lock(mutex_);
  if (proxy_) {
    return proxy_;
  }

  if (!service_hp_) {
    auto location = VERIFY_RESULT(master_client_->GetStatefulServiceLocation(service_kind_));
    auto* host_port = GetHostPort(&location);
    SCHECK(
        host_port && host_port->has_host(), IllegalState, "Service host is invalid: $0",
        location.DebugString());
    service_hp_ = std::make_shared<HostPort>(HostPort::FromPB(*host_port));
  }

  VLOG(3) << "Connecting to " << service_name_ << " at " << *service_hp_;

  proxy_.reset(make_proxy(proxy_cache_.get(), *service_hp_));
  return proxy_;
}

void StatefulServiceClientBase::Shutdown() {
  std::lock_guard lock(mutex_);

  if (master_client_) {
    master_client_->Shutdown();
  }

  if (messenger_) {
    messenger_->Shutdown();
  }
}
}  // namespace client
}  // namespace yb
