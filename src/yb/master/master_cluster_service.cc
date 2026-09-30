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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

#include "yb/gutil/casts.h"

#include "yb/common/ysql_auth_catalog_snapshot.h"
#include "yb/common/ysql_operation_lease.h"

#include "yb/consensus/consensus.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/master_cluster.service.h"
#include "yb/master/master_cluster_handler.h"
#include "yb/master/tablet_health_manager.h"
#include "yb/master/master_auto_flags_manager.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_service_base.h"
#include "yb/master/object_lock_info_manager.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/service_util.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/sync_point.h"
#include "yb/util/thread_pool.h"

using std::string;
using std::vector;

DEFINE_UNKNOWN_double(master_slow_get_registration_probability, 0,
              "Probability of injecting delay in GetMasterRegistration.");
DECLARE_bool(emergency_repair_mode);
DECLARE_bool(enable_ysql_tablespaces_for_placement);
DECLARE_bool(ysql_enable_auth_catalog_follower_reads);

METRIC_DEFINE_counter(
    server, ysql_auth_catalog_snapshot_acquisitions, "YSQL authentication catalog snapshots",
    yb::MetricUnit::kRequests,
    "Fresh authentication catalog snapshots established by this master leader.");
METRIC_DEFINE_counter(
    server, master_ysql_auth_snapshot_task_limit_rejections,
    "Master authentication snapshot task limit rejections", yb::MetricUnit::kRequests,
    "Authentication snapshot requests rejected because the master's task limit was reached.");
METRIC_DEFINE_counter(
    server, master_ysql_auth_snapshot_deadline_expirations,
    "Master authentication snapshot deadline expirations", yb::MetricUnit::kRequests,
    "Authentication snapshot tasks that timed out in the master's queue or during execution.");
METRIC_DEFINE_gauge_uint64(
    server, master_ysql_auth_snapshot_outstanding_tasks,
    "Master authentication snapshot outstanding tasks", yb::MetricUnit::kTasks,
    "Admitted authentication snapshot tasks queued or running on the master until response.");

DEFINE_RUNTIME_bool(master_list_raft_peers_check_is_leader, false,
    "When enabled, ListMasterRaftPeers RPC will reject "
    "with NOT_THE_LEADER if processed by a non-leader master."
);

using namespace std::literals;

namespace yb {
namespace master {

namespace {

class MasterClusterServiceImpl : public MasterServiceBase, public MasterClusterIf {
 public:
  explicit MasterClusterServiceImpl(Master* master)
      : MasterServiceBase(master), MasterClusterIf(master->metric_entity()),
        auth_catalog_snapshot_acquisitions_(
            METRIC_ysql_auth_catalog_snapshot_acquisitions.Instantiate(master->metric_entity())),
        auth_snapshot_task_limit_rejections_(
            METRIC_master_ysql_auth_snapshot_task_limit_rejections.Instantiate(
                master->metric_entity())),
        auth_snapshot_deadline_expirations_(
            METRIC_master_ysql_auth_snapshot_deadline_expirations.Instantiate(
                master->metric_entity())),
        auth_snapshot_outstanding_tasks_(
            METRIC_master_ysql_auth_snapshot_outstanding_tasks.Instantiate(
                master->metric_entity(), 0)) {}

  ~MasterClusterServiceImpl() override { Shutdown(); }

  void Shutdown() override {
    std::call_once(auth_snapshot_shutdown_, [this] {
      auth_snapshot_stopping_.store(true);
      auth_snapshot_pool_.Shutdown();
    });
  }

  void ListTabletServers(const ListTabletServersRequestPB* req,
                         ListTabletServersResponsePB* resp,
                         rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    resp->set_master_uuid(server_->permanent_uuid());
    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      return;
    }

    std::vector<std::shared_ptr<TSDescriptor>> descs;
    if (!req->primary_only()) {
      descs = server_->ts_manager()->GetAllDescriptors();
    } else {
      auto uuid_result = server_->catalog_manager_impl()->placement_uuid();
      if (!uuid_result.ok()) {
        SetupErrorAndRespond(
            resp->mutable_error(), uuid_result.status(), MasterErrorPB_Code_INTERNAL_ERROR, &rpc);
        return;
      }
      server_->ts_manager()->GetAllLiveDescriptorsInCluster(&descs, *uuid_result);
    }

    bool is_ysql_replication_info_required =
        FLAGS_enable_ysql_tablespaces_for_placement &&
        (req->has_tablespace_id() || req->has_replication_info());
    std::unique_ptr<ReplicationInfoPB> replication_info;
    if (is_ysql_replication_info_required) {
      if (req->has_tablespace_id()) {
        LOG(INFO) << "Retrieve placement info from tablespace ID";
        auto result =
            server_->catalog_manager_impl()->
                GetTablespaceReplicationInfoWithRetry(req->tablespace_id());
        if (!result.ok()) {
          SetupErrorAndRespond(resp->mutable_error(), result.status(),
                              MasterErrorPB_Code_UNKNOWN_ERROR, &rpc);
          return;
        }
        auto tablespace_replication_pb = std::move(*result);
        if (!tablespace_replication_pb.has_value()) {
          LOG(INFO) << "Could not retrieve placement info from tablespace ID "
                    << req->tablespace_id() << ". "
                    << "Default to returning all tablet servers";
          // In YSQL, CREATE TABLE using LOCATION parameter is syntactically
          // accepted but semantically ignored. In this case, replication info
          // does not exist through the tablespace ID. So skipping the filter
          // process below and returning the tablet servers retrieved via
          // GetAllLiveDescriptorsInCluster() which filters placement ID's
          // based on master primary cluster's placement ID.
          is_ysql_replication_info_required = false;
        } else {
          replication_info =
              std::make_unique<ReplicationInfoPB>(*tablespace_replication_pb);
        }
      } else if (req->has_replication_info()) {
        LOG(INFO) << "Retrieve placement info from user request";
        replication_info =
            std::make_unique<ReplicationInfoPB>(req->replication_info());
      }
    }

    auto ysql_lease_infos =
        server_->catalog_manager_impl()->object_lock_info_manager()->GetLeaseInfos();
    for (const auto& desc : descs) {
      auto l = desc->LockForRead();
      if (is_ysql_replication_info_required) {
        LOG(INFO) << "Filter TServers based on placement ID "
                  << "and cloud info against placement "
                  << replication_info->ShortDebugString();
        // Filter based on placement ID
        if (l->pb.registration().placement_uuid() !=
            replication_info->live_replicas().placement_uuid())
          continue;

        // Filter based on cloud, region, zone IDs (all IDs must match)
        bool is_cloud_match = false;
        const auto& placement_blocks =
            replication_info->live_replicas().placement_blocks();
        for (const auto& pb : placement_blocks) {
          if (CatalogManagerUtil::IsCloudInfoPrefix(
                  l->pb.registration().cloud_info(), pb.cloud_info())) {
            is_cloud_match = true;
            break;
          }
        }
        if (!is_cloud_match) continue;
        LOG(INFO) << "Placement info has matched against placement "
                  << replication_info->live_replicas().placement_uuid();
      }
      ListTabletServersResponsePB::Entry* entry = resp->add_servers();
      *entry->mutable_instance_id() = desc->GetNodeInstancePB();
      *entry->mutable_registration() = desc->GetTSRegistrationPB();
      auto last_heartbeat = desc->LastHeartbeatTime();
      if (last_heartbeat) {
        auto ms_since_heartbeat = MonoTime::Now().GetDeltaSince(last_heartbeat).ToMilliseconds();
        if (ms_since_heartbeat > std::numeric_limits<int32_t>::max()) {
          LOG(DFATAL) << entry->instance_id().permanent_uuid()
                      << " has not heartbeated since "
                      << ms_since_heartbeat;
          ms_since_heartbeat = std::numeric_limits<int32_t>::max();
        }
        entry->set_millis_since_heartbeat(narrow_cast<int>(ms_since_heartbeat));
      }
      entry->set_alive(desc->IsLive());
      desc->GetMetrics(entry->mutable_metrics());
      auto it = ysql_lease_infos.find(desc->id());
      if (it != ysql_lease_infos.end() &&
          it->second.lease_info.instance_seqno() == entry->instance_id().instance_seqno()) {
        auto& lease_info = *entry->mutable_lease_info();
        lease_info.set_is_live(it->second.lease_info.live_lease());
        lease_info.set_lease_epoch(it->second.lease_info.lease_epoch());
      }
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
      SetupErrorAndRespond(
          resp->mutable_error(), placement_uuid_result.status(), MasterErrorPB_Code_INTERNAL_ERROR,
          &rpc);
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
      *entry->mutable_instance_id() = desc->GetNodeInstancePB();
      *entry->mutable_registration() = desc->GetTSRegistrationPB();
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
    if (FLAGS_master_list_raft_peers_check_is_leader) {
      // check if the master is leader and return NOT_THE_LEADER if not
      SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
      if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
        return;
      }
    }
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
    if (role == PeerRole::LEADER && !FLAGS_emergency_repair_mode) {
      // When in emergency_repair_mode the CatalogManager is not fully loaded and leader_state will
      // be invalid. We need to allow the leader to respond to the GetMasterRegistration request so
      // that the client can then invoke DumpSysCatalogEntries and WriteSysCatalogEntry RPCs.
      if (!l.leader_status().ok()) {
        YB_LOG_EVERY_N_SECS(INFO, 5) << l.leader_status();
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
      auto s = server_->catalog_manager_impl()->DumpState(&ss, req->on_disk());
      CheckRespErrorOrSetUnknown(s, resp);
      if (!s.ok())
        return;
      resp->set_dump(title + ":\n" + ss.str());
    } else {
      LOG(INFO) << title;
      auto s = server_->catalog_manager_impl()->DumpState(&LOG(INFO), req->on_disk());
      CheckRespErrorOrSetUnknown(s, resp);
      if (!s.ok())
        return;
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
    HANDLE_ON_LEADER_WITH_LOCK(MasterClusterHandler, SetClusterConfig);
  }

  void GetYsqlAuthCatalogReadTime(
      const GetYsqlAuthCatalogReadTimeRequestPB* req,
      GetYsqlAuthCatalogReadTimeResponsePB* resp, rpc::RpcContext rpc) override {
    if (!FLAGS_ysql_enable_auth_catalog_follower_reads) {
      FillStatus(STATUS(NotSupported, "Authentication catalog follower reads are disabled"),
                 MasterErrorPB::UNKNOWN_ERROR, resp);
      rpc.RespondSuccess();
      return;
    }
    if (auth_snapshot_stopping_.load() || !auth_snapshot_pool_.options().max_workers) {
      FillStatus(STATUS(ServiceUnavailable, "Authentication snapshot workers are unavailable"),
                 MasterErrorPB::UNKNOWN_ERROR, resp);
      rpc.RespondSuccess();
      return;
    }
    if (auth_snapshot_tasks_.fetch_add(1) >= auth_snapshot_limits_.max_tasks) {
      --auth_snapshot_tasks_;
      auth_snapshot_task_limit_rejections_->Increment();
      FillStatus(STATUS(ServiceUnavailable, "Authentication snapshot task limit reached"),
                 MasterErrorPB::UNKNOWN_ERROR, resp);
      rpc.RespondSuccess();
      return;
    }
    auth_snapshot_outstanding_tasks_->Increment();
    auth_snapshot_pool_.Enqueue(new AuthSnapshotTask(
        *this, rpc::MakeTypedPBRpcContextHolder(*req, resp, std::move(rpc))));
    TEST_SYNC_POINT_CALLBACK("MasterClusterService::AuthSnapshot::Enqueued", server_);
  }

  void GetMasterClusterConfig(
      const GetMasterClusterConfigRequestPB* req, GetMasterClusterConfigResponsePB* resp,
      rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(MasterClusterHandler, GetClusterConfig);
  }

  void GetLeaderBlacklistCompletion(
      const GetLeaderBlacklistPercentRequestPB* req, GetLoadMovePercentResponsePB* resp,
      rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(MasterClusterHandler, GetLeaderBlacklistCompletionPercent);
  }

  void GetLoadMoveCompletion(
      const GetLoadMovePercentRequestPB* req, GetLoadMovePercentResponsePB* resp,
      rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITH_LOCK(MasterClusterHandler, GetLoadMoveCompletionPercent);
  }

  MASTER_SERVICE_IMPL_ON_ALL_MASTERS(
    TabletHealthManager,
    (CheckMasterTabletHealth)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    MasterClusterHandler,
    (AreLeadersOnPreferredOnly)
    (SetPreferredZones)
    (RemoveTabletServer)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    CatalogManager,
    (IsLoadBalanced)
    (IsLoadBalancerIdle)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    MasterAutoFlagsManager,
    (GetAutoFlagsConfig)
    (PromoteAutoFlags)
    (RollbackAutoFlags)
    (PromoteSingleAutoFlag)
    (DemoteSingleAutoFlag)
    (ValidateAutoFlagsConfig)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(XClusterManager,
    (GetMasterXClusterConfig)
  )

 private:
  using AuthSnapshotContext = rpc::TypedPBRpcContextHolder<
      GetYsqlAuthCatalogReadTimeRequestPB, GetYsqlAuthCatalogReadTimeResponsePB>;

  class AuthSnapshotTask : public yb::ThreadPoolTask {
   public:
    AuthSnapshotTask(MasterClusterServiceImpl& service, AuthSnapshotContext&& context)
        : service_(service), context_(std::move(context)),
          deadline_(AuthSnapshotDeadline(
              context_->GetClientDeadline(), "MasterClusterService::AuthSnapshot::Timeout")) {}

   private:
    void Run() override {
      TEST_SYNC_POINT_CALLBACK(
          "MasterClusterService::AuthSnapshot::Service", static_cast<MasterClusterIf*>(&service_));
      TEST_SYNC_POINT_CALLBACK("MasterClusterService::AuthSnapshot::Execute", service_.server_);
      service_.EstablishAuthSnapshot(context_, deadline_);
    }

    void Done(const Status& status) override {
      if (!status.ok()) {
        FillStatus(STATUS(ServiceUnavailable, "Authentication snapshot workers are shutting down"),
                   MasterErrorPB::UNKNOWN_ERROR, &context_.resp());
        context_->RespondSuccess();
      }
      --service_.auth_snapshot_tasks_;
      service_.auth_snapshot_outstanding_tasks_->Decrement();
      delete this;
    }

    MasterClusterServiceImpl& service_;
    AuthSnapshotContext context_;
    const CoarseTimePoint deadline_;
  };

  void EstablishAuthSnapshot(AuthSnapshotContext& context, CoarseTimePoint deadline) {
    auto* resp = &context.resp();
    const auto& req = context.req();
    SCOPED_LEADER_SHARED_LOCK(lock, server_->catalog_manager_impl());
    if (!lock.CheckIsInitializedAndIsLeaderOrRespond(resp, &context.context())) {
      return;
    }
    const auto epoch = lock.epoch();
    auto establish = [&]() -> Status {
      SCHECK(!auth_snapshot_stopping_.load(), ServiceUnavailable,
             "Authentication snapshot workers are shutting down");
      SCHECK(CoarseMonoClock::Now() < deadline, TimedOut,
             "Authentication snapshot expired in queue");
      SCHECK(FLAGS_ysql_enable_auth_catalog_follower_reads, NotSupported,
             "Authentication catalog follower reads are disabled");
      SCHECK(server_->snapshot_coordinator().PitrDisabled(), IllegalState,
             "Authentication catalog follower reads require a universe created with PITR disabled");
      if (req.has_propagated_hybrid_time()) {
        SCHECK(!HybridTime(req.propagated_hybrid_time()).is_special(), InvalidArgument,
               "Invalid propagated hybrid time");
      }
      server::UpdateClock(req, server_->clock());
      // Select T only after dequeue and ready-leader/capability validation. Safe time alone can
      // lag completed writes: cover bounded skew, then wait under the leader lease.
      auto read_time = server_->clock()->MaxGlobalNow();
      TEST_SYNC_POINT_CALLBACK("MasterClusterService::AuthSnapshot::ReadTime", &read_time);
      const auto peer = server_->sys_catalog().tablet_peer();
      SCHECK(peer, ServiceUnavailable, "System catalog peer is unavailable");
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      lock.Unlock();
      RETURN_NOT_OK(ResultToStatus(tablet->SafeTime(
          tablet::RequireLease::kTrue, read_time, deadline)));
      TEST_SYNC_POINT_CALLBACK("MasterClusterService::AuthSnapshot::AfterSafeTime", server_);
      SCHECK(!auth_snapshot_stopping_.load(), ServiceUnavailable,
             "Authentication snapshot workers are shutting down");
      SCOPED_LEADER_SHARED_LOCK(recheck, server_->catalog_manager_impl(), epoch.leader_term);
      auto consensus = VERIFY_RESULT(peer->GetConsensus());
      const auto leader_state = consensus->GetLeaderState();
      if (!recheck.IsInitializedAndIsLeader() || !leader_state.ok() ||
          leader_state.term != epoch.leader_term) {
        return STATUS(IllegalState, "Master leadership changed while establishing auth snapshot",
                      MasterError(MasterErrorPB::NOT_THE_LEADER));
      }
      SCHECK(CoarseMonoClock::Now() < deadline, TimedOut,
             "Authentication snapshot expired before publishing read time");
      resp->set_read_time(read_time.ToUint64());
      auth_catalog_snapshot_acquisitions_->Increment();
      return Status::OK();
    };
    const auto status = establish();
    if (status.IsTimedOut()) {
      auth_snapshot_deadline_expirations_->Increment();
    }
    CheckRespErrorOrSetUnknown(status, resp);
    context->RespondSuccess();
  }

  scoped_refptr<Counter> auth_catalog_snapshot_acquisitions_;
  scoped_refptr<Counter> auth_snapshot_task_limit_rejections_;
  scoped_refptr<Counter> auth_snapshot_deadline_expirations_;
  scoped_refptr<AtomicGauge<uint64_t>> auth_snapshot_outstanding_tasks_;
  std::atomic<size_t> auth_snapshot_tasks_{0};
  std::atomic<bool> auth_snapshot_stopping_{false};
  std::once_flag auth_snapshot_shutdown_;
  const YsqlAuthSnapshotLimits auth_snapshot_limits_ = MasterAuthSnapshotLimits();
  // The admission counter bounds both queued and running tasks. Done also releases slots and
  // responds for tasks discarded by Shutdown, unlike a plain EnqueueFunctor.
  YBThreadPool auth_snapshot_pool_{ThreadPoolOptions{
      .name = "master_auth_snapshot",
      .max_workers = auth_snapshot_limits_.workers,
      .min_workers = 0,
  }};
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterClusterService(Master* master) {
  return std::make_unique<MasterClusterServiceImpl>(master);
}

} // namespace master
} // namespace yb
