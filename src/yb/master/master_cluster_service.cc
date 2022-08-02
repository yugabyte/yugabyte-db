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

#include "yb/gutil/casts.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_cluster.service.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/util/service_util.h"

DEFINE_double(master_slow_get_registration_probability, 0,
              "Probability of injecting delay in GetMasterRegistration.");

using namespace std::literals;

namespace yb {
namespace master {

namespace {

class MasterClusterServiceImpl : public MasterServiceBase, public MasterClusterIf {
 public:
  explicit MasterClusterServiceImpl(Master* master)
      : MasterServiceBase(master), MasterClusterIf(master->metric_entity()) {}

  void ListTabletServers(const ListTabletServersRequestPB* req,
                         ListTabletServersResponsePB* resp,
                         rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      return;
    }

    std::vector<std::shared_ptr<TSDescriptor> > descs;
    if (!req->primary_only()) {
      server_->ts_manager()->GetAllDescriptors(&descs);
    } else {
      auto uuid_result = server_->catalog_manager_impl()->placement_uuid();
      if (!uuid_result.ok()) {
        return;
      }
      server_->ts_manager()->GetAllLiveDescriptorsInCluster(&descs, *uuid_result);
    }

    for (const std::shared_ptr<TSDescriptor>& desc : descs) {
      ListTabletServersResponsePB::Entry* entry = resp->add_servers();
      auto ts_info = *desc->GetTSInformationPB();
      *entry->mutable_instance_id() = std::move(*ts_info.mutable_tserver_instance());
      *entry->mutable_registration() = std::move(*ts_info.mutable_registration());
      entry->set_millis_since_heartbeat(
          narrow_cast<int>(desc->TimeSinceHeartbeat().ToMilliseconds()));
      entry->set_alive(desc->IsLive());
      desc->GetMetrics(entry->mutable_metrics());
    }
    rpc.RespondSuccess();
  }

  void ListLiveTabletServers(const ListLiveTabletServersRequestPB* req,
                             ListLiveTabletServersResponsePB* resp,
                             rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      return;
    }
    auto placement_uuid_result = server_->catalog_manager_impl()->placement_uuid();
    if (!placement_uuid_result.ok()) {
      return;
    }
    string placement_uuid = *placement_uuid_result;

    vector<std::shared_ptr<TSDescriptor> > descs;
    auto blacklist_result = server_->catalog_manager()->BlacklistSetFromPB();
    BlacklistSet blacklist = blacklist_result.ok() ? *blacklist_result : BlacklistSet();

    server_->ts_manager()->GetAllLiveDescriptors(&descs);

    for (const std::shared_ptr<TSDescriptor>& desc : descs) {
      // Skip descriptors which are (not "live") OR (blacklisted AND have no tablets)
      if (!desc->IsLive() || (server_->ts_manager()->IsTsBlacklisted(desc, blacklist)
        && desc->num_live_replicas() == 0)) {
        continue;
      }
      ListLiveTabletServersResponsePB::Entry* entry = resp->add_servers();
      auto ts_info = *desc->GetTSInformationPB();
      *entry->mutable_instance_id() = std::move(*ts_info.mutable_tserver_instance());
      *entry->mutable_registration() = std::move(*ts_info.mutable_registration());
      bool isPrimary = server_->ts_manager()->IsTsInCluster(desc, placement_uuid);
      entry->set_isfromreadreplica(!isPrimary);
    }
    rpc.RespondSuccess();
  }

  void ListMasters(
      const ListMastersRequestPB* req,
      ListMastersResponsePB* resp,
      rpc::RpcContext rpc) override {
    std::vector<ServerEntryPB> masters;
    Status s = server_->ListMasters(&masters);
    if (s.ok()) {
      for (const ServerEntryPB& master : masters) {
        resp->add_masters()->CopyFrom(master);
      }
      rpc.RespondSuccess();
    } else {
      SetupErrorAndRespond(resp->mutable_error(), s, MasterErrorPB_Code_UNKNOWN_ERROR, &rpc);
    }
  }

  void ListMasterRaftPeers(
      const ListMasterRaftPeersRequestPB* req,
      ListMasterRaftPeersResponsePB* resp,
      rpc::RpcContext rpc) override {
    std::vector<consensus::RaftPeerPB> masters;
    Status s = server_->ListRaftConfigMasters(&masters);
    if (s.ok()) {
      for (const consensus::RaftPeerPB& master : masters) {
        resp->add_masters()->CopyFrom(master);
      }
      rpc.RespondSuccess();
    } else {
      SetupErrorAndRespond(resp->mutable_error(), s, MasterErrorPB_Code_UNKNOWN_ERROR, &rpc);
    }
  }

  void GetMasterRegistration(const GetMasterRegistrationRequestPB* req,
                             GetMasterRegistrationResponsePB* resp,
                             rpc::RpcContext rpc) override {
    // instance_id must always be set in order for status pages to be useful.
    if (RandomActWithProbability(FLAGS_master_slow_get_registration_probability)) {
      std::this_thread::sleep_for(20s);
    }
    resp->mutable_instance_id()->CopyFrom(server_->instance_pb());
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
      return;
    }
    Status s = server_->GetMasterRegistration(resp->mutable_registration());
    CheckRespErrorOrSetUnknown(s, resp);
    auto role = server_->catalog_manager_impl()->Role();
    if (role == PeerRole::LEADER) {
      if (!l.leader_status().ok()) {
        YB_LOG_EVERY_N_SECS(INFO, 1)
            << "Patching role from leader to follower because of: " << l.leader_status()
            << THROTTLE_MSG;
        role = PeerRole::FOLLOWER;
      }
    }
    resp->set_role(role);
    rpc.RespondSuccess();
  }

  void IsMasterLeaderServiceReady(
      const IsMasterLeaderReadyRequestPB* req, IsMasterLeaderReadyResponsePB* resp,
      rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      return;
    }

    rpc.RespondSuccess();
  }

  void DumpState(
      const DumpMasterStateRequestPB* req,
      DumpMasterStateResponsePB* resp,
      rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
      return;
    }

    const string role = (req->has_peers_also() && req->peers_also() ? "Leader" : "Follower");
    const string title = role + " Master " + server_->instance_pb().permanent_uuid();

    if (req->return_dump_as_string()) {
      std::ostringstream ss;
      server_->catalog_manager_impl()->DumpState(&ss, req->on_disk());
      resp->set_dump(title + ":\n" + ss.str());
    } else {
      LOG(INFO) << title;
      server_->catalog_manager_impl()->DumpState(&LOG(INFO), req->on_disk());
    }

    if (req->has_peers_also() && req->peers_also()) {
      std::vector<consensus::RaftPeerPB> masters_raft;
      Status s = server_->ListRaftConfigMasters(&masters_raft);
      CheckRespErrorOrSetUnknown(s, resp);

      if (!s.ok())
        return;

      LOG(INFO) << "Sending dump command to " << masters_raft.size()-1 << " peers.";

      // Remove our entry before broadcasting to all peers.
      bool found = false;
      for (auto it = masters_raft.begin(); it != masters_raft.end(); it++) {
        if (server_->instance_pb().permanent_uuid() == it->permanent_uuid()) {
          masters_raft.erase(it);
          found = true;
          break;
        }
      }

      LOG_IF(DFATAL, !found) << "Did not find leader in Raft config: "
                             << server_->instance_pb().permanent_uuid();

      s = server_->catalog_manager_impl()->PeerStateDump(masters_raft, req, resp);
      CheckRespErrorOrSetUnknown(s, resp);
    }

    rpc.RespondSuccess();
  }

  void ChangeLoadBalancerState(
      const ChangeLoadBalancerStateRequestPB* req, ChangeLoadBalancerStateResponsePB* resp,
      rpc::RpcContext rpc) override {
    // This should work on both followers and leaders, in order to cover leader failover!
    if (req->has_is_enabled()) {
      LOG(INFO) << "Changing balancer state to " << req->is_enabled();
      server_->catalog_manager_impl()->SetLoadBalancerEnabled(req->is_enabled());
    }

    rpc.RespondSuccess();
  }

  void GetLoadBalancerState(
      const GetLoadBalancerStateRequestPB* req, GetLoadBalancerStateResponsePB* resp,
      rpc::RpcContext rpc) override {
    resp->set_is_enabled(server_->catalog_manager_impl()->IsLoadBalancerEnabled());
    rpc.RespondSuccess();
  }

  void RemovedMasterUpdate(const RemovedMasterUpdateRequestPB* req,
                           RemovedMasterUpdateResponsePB* resp,
                           rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
      return;
    }

    Status s = server_->GoIntoShellMode();
    CheckRespErrorOrSetUnknown(s, resp);
    rpc.RespondSuccess();
  }

  void ChangeMasterClusterConfig(
    const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp,
    rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(CatalogManager, SetClusterConfig);
  }

  void GetMasterClusterConfig(
      const GetMasterClusterConfigRequestPB* req, GetMasterClusterConfigResponsePB* resp,
      rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(CatalogManager, GetClusterConfig);
  }

  void GetLeaderBlacklistCompletion(
      const GetLeaderBlacklistPercentRequestPB* req, GetLoadMovePercentResponsePB* resp,
      rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(CatalogManager, GetLeaderBlacklistCompletionPercent);
  }

  void GetLoadMoveCompletion(
      const GetLoadMovePercentRequestPB* req, GetLoadMovePercentResponsePB* resp,
      rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(CatalogManager, GetLoadMoveCompletionPercent);
  }

  void GetAutoFlagsConfig(
      const GetAutoFlagsConfigRequestPB* req, GetAutoFlagsConfigResponsePB* resp,
      rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      return;
    }

    *resp->mutable_config() = server_->GetAutoFlagConfig();

    rpc.RespondSuccess();
  }

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    CatalogManager,
    (AreLeadersOnPreferredOnly)
    (IsLoadBalanced)
    (IsLoadBalancerIdle)
    (SetPreferredZones)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterClusterService(Master* master) {
  return std::make_unique<MasterClusterServiceImpl>(master);
}

} // namespace master
} // namespace yb
