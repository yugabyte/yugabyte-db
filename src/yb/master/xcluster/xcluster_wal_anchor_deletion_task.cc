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

#include "yb/master/xcluster/xcluster_wal_anchor_deletion_task.h"

#include "yb/client/xcluster_client.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_target_manager.h"

DEFINE_test_flag(bool, xcluster_wal_anchor_deletion_skip_marker_clear, false,
    "If set, send the WAL_ANCHOR delete RPC but skip clearing the pending deletion marker.");

namespace yb::master {

XClusterWalAnchorDeletionTask::XClusterWalAnchorDeletionTask(
    CatalogManager& catalog_manager, rpc::Messenger& messenger,
    XClusterTargetManager& target_manager, const LeaderEpoch& epoch)
    : MultiStepMonitoredTask(*catalog_manager.AsyncTaskPool(), messenger),
      catalog_manager_(catalog_manager),
      xcluster_manager_(*catalog_manager.GetXClusterManagerImpl()),
      target_manager_(target_manager),
      epoch_(epoch) {}

server::MonitoredTaskType XClusterWalAnchorDeletionTask::type() const {
  return server::MonitoredTaskType::kXClusterWalAnchorDeletion;
}

std::string XClusterWalAnchorDeletionTask::type_name() const {
  return "Delete xCluster WAL anchor streams";
}

std::string XClusterWalAnchorDeletionTask::description() const {
  return "Delete pending xCluster WAL anchor streams";
}

Status XClusterWalAnchorDeletionTask::RegisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(2);
  RETURN_NOT_OK(target_manager_.RegisterWalAnchorDeletionTask(shared_from_this()));
  return xcluster_manager_.RegisterMonitoredTask(shared_from_this());
}

void XClusterWalAnchorDeletionTask::UnregisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(2);
  xcluster_manager_.UnRegisterMonitoredTask(shared_from_this());
  target_manager_.UnRegisterWalAnchorDeletionTask(shared_from_this());
}

Status XClusterWalAnchorDeletionTask::ValidateRunnable() {
  return catalog_manager_.GetValidateEpochFunc()(epoch_);
}

Status XClusterWalAnchorDeletionTask::FirstStep() {
  const auto pending_table_ids = target_manager_.GetPendingWalAnchorDeletionTables();

  std::vector<TableId> tables_to_forget;
  for (const auto& consumer_table_id : pending_table_ids) {
    auto table_info = catalog_manager_.GetTableInfo(consumer_table_id);
    if (!table_info) {
      // Table is dropped.
      tables_to_forget.push_back(consumer_table_id);
      continue;
    }

    TableId source_table_id;
    NamespaceId namespace_id;
    {
      auto l = table_info->LockForRead();
      source_table_id = l->pb.xcluster_pending_wal_anchor_deletion_source_table_id();
      namespace_id = l->pb.namespace_id();
    }
    if (source_table_id.empty()) {
      // Marker is already cleared, so the anchor stream is gone.
      tables_to_forget.push_back(consumer_table_id);
      continue;
    }

    const auto replication_group_id = FindAutomaticModeReplicationGroup(namespace_id);
    if (replication_group_id.empty()) {
      // Replication group is gone.
      tables_to_forget.push_back(consumer_table_id);
      continue;
    }

    // The source deletes all the anchors of a replication group in one call, so batch by group.
    auto& batch = batch_by_group_[replication_group_id];
    batch.consumer_table_ids.push_back(consumer_table_id);
    batch.source_table_ids.push_back(source_table_id);
  }

  if (!tables_to_forget.empty()) {
    LOG_WITH_PREFIX(INFO) << "Stopped tracking pending WAL_ANCHOR stream deletions for "
                          << yb::ToString(tables_to_forget);
    target_manager_.RemovePendingWalAnchorDeletionsFromSet(tables_to_forget);
  }

  if (batch_by_group_.empty()) {
    Complete();
    return Status::OK();
  }

  ScheduleNextStep(
      std::bind(&XClusterWalAnchorDeletionTask::SendDeleteRequests, this), "SendDeleteRequests");
  return Status::OK();
}

Status XClusterWalAnchorDeletionTask::SendDeleteRequests() {
  // TODO(#33455): Clear stale WAL_ANCHOR deletion markers in case of failover.
  for (const auto& [replication_group_id, batch] : batch_by_group_) {
    auto status = DeleteWalAnchorStreamsOnSource(replication_group_id, batch);
    if (!status.ok()) {
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 30)
          << "Failed to delete " << batch.source_table_ids.size()
          << " xCluster WAL_ANCHOR stream(s) on source in replication group "
          << replication_group_id << ": " << status << ". Will retry.";
    }
  }

  Complete();
  return Status::OK();
}

Status XClusterWalAnchorDeletionTask::DeleteWalAnchorStreamsOnSource(
    const xcluster::ReplicationGroupId& replication_group_id, const Batch& batch) {
  auto replication_group = catalog_manager_.GetUniverseReplication(replication_group_id);
  SCHECK_FORMAT(
      replication_group, NotFound, "Replication group $0 not found", replication_group_id);

  auto remote_client = VERIFY_RESULT(GetXClusterRemoteClientHolder(*replication_group));
  RETURN_NOT_OK(remote_client->GetXClusterClient().DeleteXClusterWalAnchorStreams(
      replication_group_id, batch.source_table_ids));

  if (FLAGS_TEST_xcluster_wal_anchor_deletion_skip_marker_clear) {
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO)
      << "Source confirmed the anchor streams are gone, clearing the durable markers for "
      << yb::ToString(batch.consumer_table_ids);
  return target_manager_.ClearWalAnchorDeletionMarkers(batch.consumer_table_ids, epoch_);
}

xcluster::ReplicationGroupId XClusterWalAnchorDeletionTask::FindAutomaticModeReplicationGroup(
    const NamespaceId& namespace_id) {
  for (const auto& universe : catalog_manager_.GetAllUniverseReplications()) {
    if (universe->IsAutomaticDdlMode() && HasNamespace(*universe, namespace_id)) {
      return universe->ReplicationGroupId();
    }
  }
  return {};
}

}  // namespace yb::master
