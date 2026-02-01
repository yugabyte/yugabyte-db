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

#include "yb/tserver/connectivity_poller.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/server/server_base.proxy.h"
#include "yb/server/server_base.h"

#include "yb/tserver/master_leader_poller.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/physical_time.h"

DEFINE_RUNTIME_uint64(connectivity_check_interval_ms, 60000,
    "Milliseconds interval to check connectivity between cluster nodes");

namespace yb::tserver {

namespace {

class ClusterState {
 public:
  void UpdateNode(const ConnectivityEntryPB& node_info) {
    VLOG(2) << Format("Update node $0", node_info);
    auto now = WallClock()->Now();
    if (!now.ok()) {
      WARN_NOT_OK(now, "Failed to obtain current time");
      return;
    }
    std::lock_guard lock(mutex_);
    auto& node_info_ref = nodes_[node_info.uuid()];
    node_info_ref = node_info;
    node_info_ref.set_last_seen_us_since_epoch(now->time_point);
  }

  ConnectivityStateResponsePB ToPB() {
    ConnectivityStateResponsePB result;
    std::lock_guard lock(mutex_);
    for (const auto& [uuid, info] : nodes_) {
      *result.mutable_entries()->Add() = info;
    }
    return result;
  }

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, ConnectivityEntryPB> nodes_ GUARDED_BY(mutex_);
};

using ClusterStatePtr = std::shared_ptr<ClusterState>;

class PingRequest : public std::enable_shared_from_this<PingRequest> {
 public:
  PingRequest(const ClusterStatePtr& cluster_state, rpc::ProxyCache& proxy_cache,
              const ConnectivityEntryPB& node_info)
      : cluster_state_(cluster_state),
        node_info_(node_info),
        proxy_(&proxy_cache, HostPortFromPB(node_info_.endpoint())) {
    controller_.set_timeout(MonoDelta::FromMilliseconds(FLAGS_connectivity_check_interval_ms));
  }

  void Start() {
    start_time_ = MonoTime::Now();
    proxy_.PingAsync(req_, &resp_, &controller_, [self = shared_from_this()]() {
      self->HandleResponse();
    });
  }

 private:
  void HandleResponse() {
    if (controller_.status().ok()) {
      node_info_.set_ping_us((MonoTime::Now() - start_time_).ToMicroseconds());
      cluster_state_->UpdateNode(node_info_);
    } else {
      LOG(WARNING) << Format(
          "Ping $0/$1 failed: $2", node_info_.uuid(), node_info_.endpoint(), controller_.status());
    }
  }

  const ClusterStatePtr cluster_state_;
  ConnectivityEntryPB node_info_;
  MonoTime start_time_;
  server::GenericServiceProxy proxy_;
  server::PingRequestPB req_;
  server::PingResponsePB resp_;
  rpc::RpcController controller_;
};

} // namespace

class ConnectivityPoller::Impl : public MasterLeaderPollerInterface {
 public:
  Impl(server::RpcServerBase& server, const std::string& uuid)
      : uuid_(uuid),
        cloud_info_(server.MakeCloudInfoPB()),
        log_prefix_(server::MakeServerLogPrefix(uuid)),
        finder_(server.messenger(), server.proxy_cache(), nullptr),
        poll_scheduler_(finder_, *this) {
  }

  Status Start() {
    return poll_scheduler_.Start();
  }

  void Shutdown() {
    poll_scheduler_.Shutdown();
  }

  void UpdateMasterAddresses(server::MasterAddressesPtr master_addresses) {
    poll_scheduler_.UpdateMasterAddresses(std::move(master_addresses));
  }

  Status Poll() override {
    auto timeout = IntervalToNextPoll(0);
    if (!proxy_) {
      proxy_ = VERIFY_RESULT(finder_.CreateProxy<master::MasterClusterProxy>(timeout));
    }

    master::ListTabletServersRequestPB req;
    master::ListTabletServersResponsePB resp;
    rpc::RpcController rpc_controller;
    rpc_controller.set_timeout(timeout);

    {
      auto start_time = MonoTime::Now();
      RETURN_NOT_OK(proxy_->ListTabletServers(req, &resp, &rpc_controller));
      auto ping = MonoTime::Now() - start_time;

      if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
      }

      ConnectivityEntryPB master_node_info;
      master_node_info.set_server_type(ServerType::MASTER);
      master_node_info.set_uuid(resp.master_uuid());
      master_node_info.set_alive(true);
      master_node_info.set_ping_us(ping.ToMicroseconds());
      proxy_->proxy().remote().ToPB(master_node_info.mutable_endpoint());

      cluster_state_->UpdateNode(master_node_info);
    }

    for (const auto& server : resp.servers()) {
      if (server.instance_id().permanent_uuid() == uuid_) {
        continue;
      }
      ConnectivityEntryPB node_info;
      node_info.set_server_type(ServerType::TABLET_SERVER);
      node_info.set_uuid(server.instance_id().permanent_uuid());
      node_info.set_alive(server.alive());
      *node_info.mutable_endpoint() = DesiredHostPort(server.registration().common(), cloud_info_);
      auto request = std::make_shared<PingRequest>(
          cluster_state_, finder_.get_proxy_cache(), node_info);
      request->Start();
    }

    return Status::OK();
  }

  MonoDelta IntervalToNextPoll(int32_t consecutive_failures) override {
    auto result = MonoDelta::FromMilliseconds(FLAGS_connectivity_check_interval_ms);
    if (first_poll_) {
      first_poll_ = false;
      result = std::min(result, MonoDelta::FromSeconds(5));
    }
    return result;
  }

  void Init() override {
  }

  void ResetProxy() override {
    proxy_.reset();
  }

  std::string name() override {
    return "connectivity";
  }

  const std::string& LogPrefix() const override {
    return log_prefix_;
  }

  ConnectivityStateResponsePB State() {
    return cluster_state_->ToPB();
  }

 private:
  const std::string uuid_;
  const CloudInfoPB cloud_info_;
  const std::string log_prefix_;

  bool first_poll_ = true;
  ClusterStatePtr cluster_state_ = std::make_shared<ClusterState>();
  MasterLeaderFinder finder_;
  MasterLeaderPollScheduler poll_scheduler_;
  std::optional<master::MasterClusterProxy> proxy_;
};

ConnectivityPoller::ConnectivityPoller(server::RpcServerBase& server, const std::string& uuid)
    : impl_(new Impl(server, uuid)) {
}

ConnectivityPoller::~ConnectivityPoller() = default;

Status ConnectivityPoller::Start() {
  return impl_->Start();
}

void ConnectivityPoller::Shutdown() {
  impl_->Shutdown();
}

void ConnectivityPoller::UpdateMasterAddresses(server::MasterAddressesPtr master_addresses) {
  impl_->UpdateMasterAddresses(std::move(master_addresses));
}

ConnectivityStateResponsePB ConnectivityPoller::State() {
  return impl_->State();
}

} // namespace yb::tserver
