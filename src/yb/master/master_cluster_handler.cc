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

#include "yb/master/master_cluster_handler.h"

#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_util.h"

DEFINE_RUNTIME_int32(blacklist_progress_initial_delay_secs, yb::master::kDelayAfterFailoverSecs,
    "When a master leader failsover, the time until which the progress of load movement "
    "off the blacklisted tservers is reported as 0. This initial delay "
    "gives sufficient time for heartbeats so that we don't report"
    " a premature incorrect completion.");

namespace yb::master {

MasterClusterHandler::MasterClusterHandler(CatalogManager* catalog_manager, TSManager* ts_manager)
    : catalog_manager_(DCHECK_NOTNULL(catalog_manager)), ts_manager_(DCHECK_NOTNULL(ts_manager)) {}

Status MasterClusterHandler::GetClusterConfig(GetMasterClusterConfigResponsePB* resp) {
  *resp->mutable_cluster_config() = VERIFY_RESULT(catalog_manager_->GetClusterConfig());
  return Status::OK();
}

Status MasterClusterHandler::SetClusterConfig(
    const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  SysClusterConfigEntryPB config(req->cluster_config());

  if (config.has_server_blacklist()) {
    config.mutable_server_blacklist()->set_initial_replica_load(
        narrow_cast<int32_t>(catalog_manager_->GetNumRelevantReplicas(
            config.server_blacklist(), false /* leaders_only */)));
    LOG(INFO) << Format(
        "Set blacklist of total tservers: $0, with initial load: $1",
        config.server_blacklist().hosts().size(), config.server_blacklist().initial_replica_load());
  }
  if (config.has_leader_blacklist()) {
    config.mutable_leader_blacklist()->set_initial_leader_load(
        narrow_cast<int32_t>(catalog_manager_->GetNumRelevantReplicas(
            config.leader_blacklist(), true /* leaders_only */)));
    LOG(INFO) << Format(
        "Set leader blacklist of total tservers: $0, with initial load: $1",
        config.leader_blacklist().hosts().size(), config.leader_blacklist().initial_leader_load());
  }

  auto cluster_config = catalog_manager_->ClusterConfig();
  auto l = cluster_config->LockForWrite();
  // We should only set the config, if the caller provided us with a valid update to the
  // existing config.
  if (l->pb.version() != config.version()) {
    Status s = STATUS_SUBSTITUTE(
        IllegalState,
        "Config version does not match, got $0, but most recent one is $1. Should call Get again",
        config.version(), l->pb.version());
    return SetupError(resp->mutable_error(), MasterErrorPB::CONFIG_VERSION_MISMATCH, s);
  }

  if (config.cluster_uuid() != l->pb.cluster_uuid()) {
    Status s = STATUS(InvalidArgument, "Config cluster UUID cannot be updated");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
  }

  if (config.universe_uuid() != l->pb.universe_uuid()) {
    Status s = STATUS(InvalidArgument, "Config Universe UUID cannot be updated");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
  }

  // TODO(bogdan): should this live here?
  const ReplicationInfoPB& replication_info = config.replication_info();
  for (auto& read_replica : replication_info.read_replicas()) {
    Status s = CatalogManagerUtil::IsPlacementInfoValid(read_replica);
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
    }
    if (!read_replica.has_placement_uuid()) {
      s = STATUS(IllegalState, "All read-only clusters must have a placement uuid specified");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
    }
  }

  // Validate placement information according to rules defined.
  if (replication_info.has_live_replicas()) {
    Status s = CatalogManagerUtil::IsPlacementInfoValid(replication_info.live_replicas());
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
    }
  }

  l.mutable_data()->pb.CopyFrom(config);
  // Bump the config version, to indicate an update.
  l.mutable_data()->pb.set_version(config.version() + 1);

  LOG(INFO) << "Updating cluster config to " << config.version() + 1;

  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch.leader_term, cluster_config.get()));

  l.Commit();

  return Status::OK();
}

Status MasterClusterHandler::GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp) {
  return GetLoadMoveCompletionPercent(resp, false);
}

Status MasterClusterHandler::GetLeaderBlacklistCompletionPercent(
    GetLoadMovePercentResponsePB* resp) {
  return GetLoadMoveCompletionPercent(resp, true);
}

Status MasterClusterHandler::AreLeadersOnPreferredOnly(
    const AreLeadersOnPreferredOnlyRequestPB* req, AreLeadersOnPreferredOnlyResponsePB* resp) {
  // If we have cluster replication info, then only fetch live tservers (ignore read replicas).
  TSDescriptorVector ts_descs;
  std::string live_replicas_placement_uuid = "";
  {
    auto l = catalog_manager_->ClusterConfig()->LockForRead();
    const ReplicationInfoPB& cluster_replication_info = l->pb.replication_info();
    if (cluster_replication_info.has_live_replicas()) {
      live_replicas_placement_uuid = cluster_replication_info.live_replicas().placement_uuid();
    }
    BlacklistSet blacklist = ToBlacklistSet(GetBlacklist(l->pb, false));
    if (live_replicas_placement_uuid.empty()) {
      ts_manager_->GetAllLiveDescriptors(&ts_descs, blacklist);
    } else {
      ts_manager_->GetAllLiveDescriptorsInCluster(
          &ts_descs, live_replicas_placement_uuid, blacklist);
    }
  }

  std::vector<TableInfoPtr> tables;
  tables = catalog_manager_->GetTables(GetTablesMode::kRunning);

  auto l = catalog_manager_->ClusterConfig()->LockForRead();
  Status s = CatalogManagerUtil::AreLeadersOnPreferredOnly(
      ts_descs, l->pb.replication_info(), catalog_manager_->GetTablespaceManager(), tables);
  if (!s.ok()) {
    return SetupError(
        resp->mutable_error(), MasterErrorPB::CAN_RETRY_ARE_LEADERS_ON_PREFERRED_ONLY_CHECK, s);
  }

  return Status::OK();
}

Status MasterClusterHandler::SetPreferredZones(
    const SetPreferredZonesRequestPB* req, SetPreferredZonesResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  auto cluster_config = catalog_manager_->ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto replication_info = l.mutable_data()->pb.mutable_replication_info();

  RETURN_NOT_OK(CatalogManagerUtil::SetPreferredZones(req, replication_info));

  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  LOG(INFO) << "Updating cluster config to " << l.mutable_data()->pb.version();

  Status s = catalog_manager_->sys_catalog()->Upsert(epoch.leader_term, cluster_config.get());
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
  }

  l.Commit();

  return Status::OK();
}

Status MasterClusterHandler::GetLoadMoveCompletionPercent(
    GetLoadMovePercentResponsePB* resp, bool blacklist_leader) {
  auto l = catalog_manager_->ClusterConfig()->LockForRead();

  // Fine to pass in empty defaults if server_blacklist or leader_blacklist is not filled.
  const auto& state = GetBlacklist(l->pb, blacklist_leader);
  int64_t blacklist_replicas = catalog_manager_->GetNumRelevantReplicas(state, blacklist_leader);
  int64_t initial_load =
      (blacklist_leader) ? state.initial_leader_load() : state.initial_replica_load();
  // If we are starting up and don't find any load on the tservers, return progress as 0.
  // We expect that by blacklist_progress_initial_delay_secs time, this should go away and if the
  // load is reported as 0 on the blacklisted tservers after this time then it means that
  // the transfer is successfully complete.
  if (blacklist_replicas == 0 &&
      catalog_manager_->TimeSinceElectedLeader() <=
          MonoDelta::FromSeconds(FLAGS_blacklist_progress_initial_delay_secs)) {
    LOG(INFO) << "Master leadership has changed. Reporting progress as 0 until the catalog "
              << "manager gets the correct estimates of the remaining load on the blacklisted"
              << "tservers.";
    resp->set_percent(0);
    resp->set_total(initial_load);
    resp->set_remaining(initial_load);
    return Status::OK();
  }

  // On change of master leader, initial_load_ information may be lost temporarily. Reset to
  // current value to avoid reporting progress percent as 100. Note that doing so will report
  // progress percent as 0 instead.
  // TODO(Sanket): This might be no longer relevant after we persist and load the initial load
  // on failover. Need to investigate.
  if (initial_load < blacklist_replicas) {
    LOG(INFO) << Format(
        "Initial load: $0, current load: $1."
        " Initial load is less than the current load. Probably a master leader change."
        " Reporting progress as 0",
        state.initial_replica_load(), blacklist_replicas);
    initial_load = blacklist_replicas;
  }

  LOG(INFO) << "Blacklisted count " << blacklist_replicas << " across " << state.hosts_size()
            << " servers, with initial load " << initial_load;

  // Case when a blacklisted servers did not have any starting load.
  if (initial_load == 0) {
    resp->set_percent(100);
    return Status::OK();
  }

  resp->set_percent(100 - (static_cast<double>(blacklist_replicas) * 100 / initial_load));
  resp->set_remaining(blacklist_replicas);
  resp->set_total(initial_load);

  return Status::OK();
}
}  // namespace yb::master
