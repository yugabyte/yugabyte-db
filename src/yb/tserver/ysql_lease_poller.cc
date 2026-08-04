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

#include <chrono>

#include "yb/tserver/ysql_lease_poller.h"

#include "yb/common/ysql_operation_lease.h"

#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_rpc.h"

#include "yb/server/server_base.proxy.h"

#include "yb/tserver/master_leader_poller.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ysql_lease.h"

#include "yb/util/async_util.h"
#include "yb/util/condition_variable.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/mutex.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

DEFINE_RUNTIME_uint64(ysql_lease_refresher_rpc_timeout_ms, 15000,
                      "Timeout used for the TS->Master ysql lease refresh RPCs.");

DEFINE_RUNTIME_uint64(ysql_lease_refresher_interval_ms, 1000,
    "The interval between requests from a tablet server to the master to refresh its ysql lease.");

DEFINE_test_flag(bool, tserver_enable_ysql_lease_refresh, true,
    "Whether to enable the lease refresh RPCs tablet servers send to the master leader.");

DEFINE_test_flag(double, tserver_ysql_lease_refresh_failure_prob, 0.0,
    "Probablity to pretend we got a failure in response to a lease refresh RPC.");

DECLARE_bool(enable_object_locking_for_table_locks);

namespace yb {
namespace tserver {

std::string kThreadIdentifier = "ysql_client_lease_refresh";

class YsqlLeasePoller : public MasterLeaderPollerInterface {
 public:
  YsqlLeasePoller(
      TabletServer& server, const YsqlLeaderClientListener& listener, MasterLeaderFinder& finder);
  ~YsqlLeasePoller() = default;
  Status Poll() override;
  MonoDelta IntervalToNextPoll(int32_t consecutive_failures) override;
  void Init() override;
  void ResetProxy() override;
  std::string name() override;
  const std::string& LogPrefix() const override;
  std::future<Status> RelinquishLease(MonoDelta timeout) const;

 private:
  TabletServer& server_;
  YsqlLeaderClientListener listener_;
  MasterLeaderFinder& finder_;
  std::optional<master::MasterDdlProxy> proxy_;
};

class YsqlLeaseClient::Impl {
 public:
  Impl(TabletServer& server, const YsqlLeaderClientListener& listener);

  Impl(const Impl& other) = delete;
  void operator=(const Impl& other) = delete;

  Status Start();
  void Shutdown();
  std::future<Status> RelinquishLease(MonoDelta timeout) const;
  void UpdateMasterAddresses(server::MasterAddressesPtr master_addresses);

 private:
  MasterLeaderFinder finder_;
  YsqlLeasePoller poller_;
  MasterLeaderPollScheduler poll_scheduler_;
};

YsqlLeaseClient::YsqlLeaseClient(
    TabletServer& server, const YsqlLeaderClientListener& listener)
    : impl_(std::make_unique<YsqlLeaseClient::Impl>(server, listener)) {}

Status YsqlLeaseClient::Start() {
  return impl_->Start();
}

void YsqlLeaseClient::Shutdown() {
  impl_->Shutdown();
}

std::future<Status> YsqlLeaseClient::RelinquishLease(MonoDelta timeout) const {
  return impl_->RelinquishLease(timeout);
}

void YsqlLeaseClient::UpdateMasterAddresses(server::MasterAddressesPtr master_addresses) {
  impl_->UpdateMasterAddresses(std::move(master_addresses));
}

YsqlLeaseClient::~YsqlLeaseClient() {
  Shutdown();
}

YsqlLeaseClient::Impl::Impl(
    TabletServer& server, const YsqlLeaderClientListener& listener)
    : finder_(server.messenger(), server.proxy_cache(), nullptr),
      poller_(server, listener, finder_),
      poll_scheduler_(finder_, poller_) {}

Status YsqlLeaseClient::Impl::Start() { return poll_scheduler_.Start(); }

void YsqlLeaseClient::Impl::Shutdown() {
  poll_scheduler_.Shutdown();
}

std::future<Status> YsqlLeaseClient::Impl::RelinquishLease(MonoDelta timeout) const {
  return poller_.RelinquishLease(timeout);
}

void YsqlLeaseClient::Impl::UpdateMasterAddresses(server::MasterAddressesPtr master_addresses) {
  poll_scheduler_.UpdateMasterAddresses(std::move(master_addresses));
}

YsqlLeasePoller::YsqlLeasePoller(
    TabletServer& server, const YsqlLeaderClientListener& listener, MasterLeaderFinder& finder)
    : server_(server), listener_(listener), finder_(finder) {}

Status YsqlLeasePoller::Poll() {
  if (!FLAGS_TEST_tserver_enable_ysql_lease_refresh || !IsYsqlLeaseEnabled()) {
    return Status::OK();
  }

  auto timeout =
      MonoDelta::FromMilliseconds(FLAGS_ysql_lease_refresher_rpc_timeout_ms);
  if (!proxy_) {
    proxy_ = VERIFY_RESULT(finder_.CreateProxy<master::MasterDdlProxy>(timeout));
  }

  master::RefreshYsqlLeaseRequestPB req;
  *req.mutable_instance() = server_.instance_pb();
  auto current_lease_info = VERIFY_RESULT(server_.GetYSQLLeaseInfo());
  if (current_lease_info.is_live) {
    req.set_current_lease_epoch(current_lease_info.lease_epoch);
  }
  req.set_local_request_send_time_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         CoarseMonoClock::now().time_since_epoch())
                                         .count());
  rpc::RpcController rpc;
  rpc.set_timeout(timeout);
  master::RefreshYsqlLeaseResponsePB resp;
  RETURN_NOT_OK(proxy_->RefreshYsqlLease(req, &resp, &rpc));
  if (RandomActWithProbability(
          FLAGS_TEST_tserver_ysql_lease_refresh_failure_prob)) {
    return STATUS_FORMAT(NetworkError, "Pretending to fail ysql lease refresh RPC");
  }
  RETURN_NOT_OK(ResponseStatus(resp));
  return listener_(resp.info());
}

MonoDelta YsqlLeasePoller::IntervalToNextPoll(int32_t consecutive_failures) {
  return MonoDelta::FromMilliseconds(FLAGS_ysql_lease_refresher_interval_ms);
}

void YsqlLeasePoller::Init() {}

void YsqlLeasePoller::ResetProxy() {
  proxy_.reset();
}

std::string YsqlLeasePoller::name() { return kThreadIdentifier; }

const std::string& YsqlLeasePoller::LogPrefix() const { return server_.LogPrefix(); }

std::future<Status> YsqlLeasePoller::RelinquishLease(MonoDelta timeout) const {
  auto current_host_port = finder_.get_master_leader_hostport();
  if (current_host_port == HostPort()) {
    // HostPort isn't set. We've never sent a successful request, don't bother trying to relinquish.
    std::promise<Status> promise;
    promise.set_value(Status::OK());
    return promise.get_future();
  }
  auto proxy = master::MasterDdlProxy(&finder_.get_proxy_cache(), current_host_port);
  auto rpc = std::make_shared<rpc::RpcController>();
  rpc->set_timeout(timeout);
  master::RelinquishYsqlLeaseRequestPB req;
  *req.mutable_instance() = server_.instance_pb();
  auto resp = std::make_shared<master::RelinquishYsqlLeaseResponsePB>();
  auto promise = std::make_shared<std::promise<Status>>();
  auto callback = [promise, resp, rpc]() mutable {
    if (!rpc->status().ok()) {
      promise->set_value(rpc->status());
    } else if (resp->has_error()) {
      promise->set_value(ResponseStatus(*resp));
    } else {
      promise->set_value(Status::OK());
    }
  };
  proxy.RelinquishYsqlLeaseAsync(req, resp.get(), rpc.get(), std::move(callback));
  return promise->get_future();
}

}  // namespace tserver
}  // namespace yb
