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

#include "yb/server/server_base.proxy.h"
#include "yb/server/server_base.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/background_task.h"
#include "yb/util/cgroups.h"
#include "yb/util/physical_time.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

DEFINE_RUNTIME_uint64(connectivity_check_interval_ms, 60000,
    "Milliseconds interval to check connectivity between cluster nodes");

DECLARE_int32(heartbeat_interval_ms);

using namespace std::literals;

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
    auto last_seen = node_info.last_failure().empty()
        ? now->time_point : node_info_ref.last_seen_us_since_epoch();
    node_info_ref = node_info;
    node_info_ref.set_last_seen_us_since_epoch(last_seen);
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
    } else {
      node_info_.set_last_failure(controller_.status().ToString());
      LOG(WARNING) << Format(
          "Ping $0/$1 failed: $2", node_info_.uuid(), node_info_.endpoint(), controller_.status());
    }
    cluster_state_->UpdateNode(node_info_);
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

class ConnectivityPoller::Impl {
 public:
  Impl(TabletServer& server, const std::string& uuid)
      : server_(server),
        uuid_(uuid),
        cloud_info_(server.MakeCloudInfoPB()),
        log_prefix_(server::MakeServerLogPrefix(uuid)) {
  }

  Status Start(Cgroup* cgroup = nullptr) {
    cgroup_ = cgroup;
    const auto interval = std::chrono::milliseconds(FLAGS_connectivity_check_interval_ms);
    bg_task_ = std::make_unique<BackgroundTask>(
        [this] { Run(); }, "tserver", "connectivity", std::min(interval, 5000ms));
    return bg_task_->Init();
  }

  void Shutdown() {
    if (bg_task_) {
      bg_task_->Shutdown();
    }
  }

  ConnectivityStateResponsePB State() {
    return cluster_state_->ToPB();
  }

 private:
  void Run() {
#ifdef __linux__
    if (cgroup_) {
      WARN_NOT_OK(
          cgroup_->MoveCurrentThreadToGroup(), "Failed to move connectivity thread to cgroup");
      cgroup_ = nullptr;
    }
#endif
    WARN_NOT_OK(Poll(), LogPrefix() + "Connectivity poll failed");
    bg_task_->SetInterval(std::chrono::milliseconds(FLAGS_connectivity_check_interval_ms));
  }

  Status Poll() {
    // Alive if a heartbeat succeeded within half the poll interval, at least a few heartbeats.
    Status result;
    const auto master = server_.GetMasterHeartbeatInfo();
    if (!master.uuid.empty()) {
      const auto since_heartbeat = MonoTime::Now() - master.time;
      const auto alive_window = MonoDelta::FromMilliseconds(std::max<uint64_t>(
          FLAGS_connectivity_check_interval_ms / 2, 3 * FLAGS_heartbeat_interval_ms));
      ConnectivityEntryPB master_node_info;
      master_node_info.set_server_type(ServerType::MASTER);
      master_node_info.set_uuid(master.uuid);
      master_node_info.set_alive(since_heartbeat < alive_window);
      if (!master_node_info.alive()) {
        master_node_info.set_last_failure(master.last_failure.ok()
            ? Format("No successful heartbeat for $0", since_heartbeat)
            : Format("$0; no successful heartbeat for $1", master.last_failure, since_heartbeat));
        result = STATUS_FORMAT(
            ServiceUnavailable, "Master heartbeats failing: $0", master_node_info.last_failure());
      }
      master_node_info.set_ping_us(master.rtt.ToMicroseconds());
      HostPortToPB(master.host_port, master_node_info.mutable_endpoint());
      cluster_state_->UpdateNode(master_node_info);
    }

    for (const auto& known : server_.GetKnownTServers()) {
      const auto& instance = known.info.tserver_instance();
      if (instance.permanent_uuid() == uuid_) {
        continue;
      }
      ConnectivityEntryPB node_info;
      node_info.set_server_type(ServerType::TABLET_SERVER);
      node_info.set_uuid(instance.permanent_uuid());
      node_info.set_alive(known.alive);
      *node_info.mutable_endpoint() =
          DesiredHostPort(known.info.registration().common(), cloud_info_);
      auto request = std::make_shared<PingRequest>(
          cluster_state_, server_.proxy_cache(), node_info);
      request->Start();
    }

    return result;
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  TabletServer& server_;
  const std::string uuid_;
  const CloudInfoPB cloud_info_;
  const std::string log_prefix_;

  [[maybe_unused]] Cgroup* cgroup_ = nullptr;
  ClusterStatePtr cluster_state_ = std::make_shared<ClusterState>();
  std::unique_ptr<BackgroundTask> bg_task_;
};

ConnectivityPoller::ConnectivityPoller(TabletServer& server, const std::string& uuid)
    : impl_(new Impl(server, uuid)) {
}

ConnectivityPoller::~ConnectivityPoller() = default;

Status ConnectivityPoller::Start(Cgroup* cgroup) {
  return impl_->Start(cgroup);
}

void ConnectivityPoller::Shutdown() {
  impl_->Shutdown();
}

ConnectivityStateResponsePB ConnectivityPoller::State() {
  return impl_->State();
}

} // namespace yb::tserver
