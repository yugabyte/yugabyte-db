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

#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_rpc.h"

#include "yb/server/server_base.proxy.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/master_leader_poller.h"

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

DEFINE_test_flag(bool, tserver_enable_ysql_lease_refresh, false,
    "Whether to enable the lease refresh RPCs tablet servers send to the master leader.");

namespace yb {
namespace tserver {

class YsqlLeaseClient::Impl {
 public:
  Impl(TabletServer& server, server::MasterAddressesPtr master_addresses);
  Impl(const Impl& other) = delete;
  void operator=(const Impl& other) = delete;

  Status Start();
  Status Stop();
  void set_master_addresses(server::MasterAddressesPtr master_addresses);

 private:
  MasterLeaderFinder finder_;
  MasterLeaderPollScheduler poll_scheduler_;
};

class YsqlLeasePoller : public MasterLeaderPollerInterface {
 public:
  YsqlLeasePoller(TabletServer& server, MasterLeaderFinder& finder);
  ~YsqlLeasePoller() = default;
  Status Poll() override;
  MonoDelta IntervalToNextPoll(int32_t consecutive_failures) override;
  void Init() override;
  void ResetProxy() override;
  std::string category() override;
  std::string name() override;
  const std::string& LogPrefix() const override;

 private:
  TabletServer& server_;
  MasterLeaderFinder& finder_;
  std::optional<master::MasterDdlProxy> proxy_;
};

YsqlLeaseClient::YsqlLeaseClient(TabletServer& server, server::MasterAddressesPtr master_addresses)
    : impl_(std::make_unique<YsqlLeaseClient::Impl>(server, std::move(master_addresses))) {}
Status YsqlLeaseClient::Start() { return impl_->Start(); }
Status YsqlLeaseClient::Stop() { return impl_->Stop(); }
void YsqlLeaseClient::set_master_addresses(server::MasterAddressesPtr master_addresses) {
  impl_->set_master_addresses(std::move(master_addresses));
}
YsqlLeaseClient::~YsqlLeaseClient() {
  WARN_NOT_OK(Stop(), "Unable to stop ysql client lease thread");
}

YsqlLeaseClient::Impl::Impl(TabletServer& server, server::MasterAddressesPtr master_addresses)
    : finder_(server.messenger(), server.proxy_cache(), std::move(master_addresses)),
      poll_scheduler_(finder_, std::make_unique<YsqlLeasePoller>(server, finder_)) {}

Status YsqlLeaseClient::Impl::Start() { return poll_scheduler_.Start(); }

Status YsqlLeaseClient::Impl::Stop() { return poll_scheduler_.Stop(); }

void YsqlLeaseClient::Impl::set_master_addresses(server::MasterAddressesPtr master_addresses) {
  finder_.set_master_addresses(std::move(master_addresses));
}

YsqlLeasePoller::YsqlLeasePoller(TabletServer& server, MasterLeaderFinder& finder)
    : server_(server), finder_(finder) {}

Status YsqlLeasePoller::Poll() {
  if (!FLAGS_TEST_tserver_enable_ysql_lease_refresh) {
    return Status::OK();
  }

  auto timeout =
      MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_ysql_lease_refresher_rpc_timeout_ms));
  if (!proxy_) {
    auto hostport = VERIFY_RESULT(finder_.UpdateMasterLeaderHostPort(timeout));
    proxy_ = master::MasterDdlProxy(&finder_.get_proxy_cache(), hostport);
  }

  master::RefreshYsqlLeaseRequestPB req;
  *req.mutable_instance() = server_.instance_pb();
  rpc::RpcController rpc;
  rpc.set_timeout(timeout);
  master::RefreshYsqlLeaseResponsePB resp;
  MonoTime pre_request_time = MonoTime::Now();
  RETURN_NOT_OK(proxy_->RefreshYsqlLease(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return server_.ProcessLeaseUpdate(resp.info(), pre_request_time);
}

MonoDelta YsqlLeasePoller::IntervalToNextPoll(int32_t consecutive_failures) {
  return MonoDelta::FromMilliseconds(FLAGS_ysql_lease_refresher_interval_ms);
}

void YsqlLeasePoller::Init() {}

void YsqlLeasePoller::ResetProxy() { proxy_ = std::nullopt; }

std::string YsqlLeasePoller::category() { return "ysql_client_lease_refresh"; }

std::string YsqlLeasePoller::name() { return "ysql_client_lease_refresher"; }

const std::string& YsqlLeasePoller::LogPrefix() const { return server_.LogPrefix(); }

}  // namespace tserver
}  // namespace yb
