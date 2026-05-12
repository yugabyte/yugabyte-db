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

#include "yb/master/xcluster/xcluster_failover_task.h"

#include "yb/common/constants.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/xcluster/xcluster_target_manager.h"

DEFINE_RUNTIME_uint32(xcluster_failover_restoration_wait_secs, 120,
    "Maximum number of seconds to wait for an xCluster failover restoration to finish before "
    "abandoning the wait and cleaning up the snapshot.");
DEFINE_RUNTIME_uint32(xcluster_failover_restoration_poll_interval_ms, 1000,
    "Delay, in milliseconds, between polls for the xCluster failover restoration state.");
DEFINE_RUNTIME_uint32(xcluster_failover_snapshot_delete_retry_secs, 30,
    "Number of seconds to keep retrying to delete an xCluster failover snapshot before giving up.");
DEFINE_RUNTIME_uint32(xcluster_failover_task_timeout_secs, 600,
    "Maximum number of seconds for the xCluster failover task to complete pre-restoration steps "
    "(pausing replication, creating and waiting for snapshots).");
DEFINE_RUNTIME_int32(xcluster_failover_snapshot_retention_hours, 1,
    "Retention time, in hours, for snapshots created during xCluster failover restore.");

DEFINE_test_flag(bool, pause_xcluster_failover_before_delete_replication, false,
    "If true, pause the xCluster failover task before deleting the replication group.");
DEFINE_test_flag(string, xcluster_failover_fail_restore_namespace_id, "",
    "If set, fail the xCluster failover restore when the given namespace id is being restored.");
DEFINE_test_flag(bool, xcluster_failover_fail_create_snapshot, false,
    "If set, fail the xCluster failover task at the create snapshot step.");
DEFINE_test_flag(bool, xcluster_failover_fail_delete_replication, false,
    "If set, simulate a failure when deleting the replication group during failover.");

namespace yb::master {

const CollectFlags kFailoverSnapshotFlags{
    CollectFlag::kIncludeParentColocatedTable, CollectFlag::kAddIndexes};

XClusterFailoverTask::XClusterFailoverTask(
    Master* master, ThreadPool& async_task_pool, rpc::Messenger& messenger,
    XClusterManager& xcluster_manager, XClusterTargetManager& target_manager,
    const xcluster::ReplicationGroupId& replication_group_id,
    std::vector<NamespaceId> namespaces, const LeaderEpoch& epoch,
    StdStatusCallback completion_callback)
    : MultiStepMonitoredTask(async_task_pool, messenger),
      master_(DCHECK_NOTNULL(master)),
      xcluster_manager_(xcluster_manager),
      target_manager_(target_manager),
      replication_group_id_(replication_group_id),
      epoch_(epoch),
      deadline_(CoarseMonoClock::now() +
                MonoDelta::FromSeconds(FLAGS_xcluster_failover_task_timeout_secs)) {
  completion_callback_ = std::move(completion_callback);
  for (auto& namespace_id : namespaces) {
    FailoverInfo info;
    info.namespace_id = std::move(namespace_id);
    failovers_.push_back(std::move(info));
  }
}

server::MonitoredTaskType XClusterFailoverTask::type() const {
  return server::MonitoredTaskType::kXClusterFailover;
}

std::string XClusterFailoverTask::type_name() const {
  return "xCluster failover";
}

std::string XClusterFailoverTask::description() const {
  return Format("xCluster failover for $0", replication_group_id_);
}

Status XClusterFailoverTask::FirstStep() {
  ScheduleNextStep(
      std::bind(&XClusterFailoverTask::PauseReplicationAndFetchSafeTimes, this),
      "PauseReplicationAndFetchSafeTimes");
  return Status::OK();
}

Status XClusterFailoverTask::RegisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(2);
  return xcluster_manager_.RegisterUniqueFailoverTask(replication_group_id_, shared_from_this());
}

void XClusterFailoverTask::UnregisterTask() {
  VLOG_WITH_PREFIX_AND_FUNC(2);
  xcluster_manager_.UnRegisterFailoverTask(replication_group_id_, shared_from_this());
}

Status XClusterFailoverTask::ValidateRunnable() {
  return master_->catalog_manager_impl()->GetValidateEpochFunc()(epoch_);
}

Status XClusterFailoverTask::PauseReplicationAndFetchSafeTimes() {
  RETURN_NOT_OK(target_manager_.SetReplicationGroupEnabled(
      replication_group_id_, /*is_enabled=*/false, epoch_, deadline_));
  RETURN_NOT_OK(master_->tablet_split_manager().PrepareForSnapshotRestore(
      deadline_, kXClusterFailoverFeatureName));
  RETURN_NOT_OK(target_manager_.RefreshXClusterSafeTimeMap(epoch_));

  for (auto& failover : failovers_) {
    auto safe_time_result = target_manager_.GetXClusterSafeTime(failover.namespace_id);
    RETURN_NOT_OK(safe_time_result);
    auto safe_time = *safe_time_result;
    SCHECK(
        !safe_time.is_special(), IllegalState,
        Format("Invalid xCluster safe time for namespace $0 in replication group $1",
               failover.namespace_id, replication_group_id_));
    failover.restore_time = safe_time;
  }

  std::vector<std::pair<NamespaceId, HybridTime>> namespace_to_restore_time;
  for (const auto& failover : failovers_) {
    namespace_to_restore_time.emplace_back(failover.namespace_id, failover.restore_time);
  }
  LOG(INFO) << "Validating xCluster failover for replication group "
            << replication_group_id_ << " with namespaces and target safe times: "
            << AsString(namespace_to_restore_time);

  ScheduleNextStep(
      std::bind(&XClusterFailoverTask::CreateSnapshotsStep, this), "CreateSnapshots");
  return Status::OK();
}

Status XClusterFailoverTask::CreateSnapshotsStep() {
  if (PREDICT_FALSE(FLAGS_TEST_xcluster_failover_fail_create_snapshot)) {
    return STATUS(Aborted, "Simulated snapshot creation failure");
  }
  for (auto& failover : failovers_) {
    auto snapshot_id = VERIFY_RESULT(master_->snapshot_coordinator().Create(
        failover.namespace_id,
        /*imported=*/false,
        kFailoverSnapshotFlags,
        epoch_.leader_term,
        deadline_,
        FLAGS_xcluster_failover_snapshot_retention_hours));
    failover.snapshot_id = snapshot_id;
  }

  ScheduleNextStep(
      std::bind(&XClusterFailoverTask::RestoreSnapshotsStep, this),
      "RestoreSnapshots");
  return Status::OK();
}

Status XClusterFailoverTask::RestoreSnapshotsStep() {
  SCHECK(
      !failovers_.empty(), IllegalState,
      Format("Replication group $0 has no namespaces to snapshot", replication_group_id_));
  for (auto& failover : failovers_) {
    SCHECK(
        !failover.snapshot_id.IsNil(), IllegalState,
        Format("No snapshot found for namespace $0 in replication group $1",
               failover.namespace_id, replication_group_id_));
    const auto& snapshot_id = failover.snapshot_id;
    const auto& restore_time = failover.restore_time;

    RETURN_NOT_OK(master_->snapshot_coordinator().WaitForSnapshotToComplete(
        snapshot_id, deadline_));

    failover.restoration_id = VERIFY_RESULT(master_->snapshot_coordinator().Restore(
        snapshot_id, restore_time, epoch_.leader_term));
  }

  failover_restoration_deadline_ =
      CoarseMonoClock::now() +
      MonoDelta::FromSeconds(FLAGS_xcluster_failover_restoration_wait_secs * failovers_.size());
  ScheduleNextStep(
      std::bind(&XClusterFailoverTask::PollForCurrentRestoration, this),
      "PollForCurrentRestoration");
  return Status::OK();
}

Status XClusterFailoverTask::PollForCurrentRestoration() {
  const auto& failover = failovers_[current_restoration_index_];
  const auto& restoration_id = failover.restoration_id;
  const auto& snapshot_id = failover.snapshot_id;
  const auto& namespace_id = failover.namespace_id;

  SCHECK(
      !restoration_id.IsNil(), IllegalState,
      Format("No restoration found for namespace $0 in replication group $1", namespace_id,
             replication_group_id_));
  SCHECK(
      !snapshot_id.IsNil(), IllegalState,
      Format("No snapshot found for namespace $0 in replication group $1", namespace_id,
             replication_group_id_));

  if (CoarseMonoClock::now() >= failover_restoration_deadline_) {
    return STATUS_FORMAT(
        TimedOut, "Timed out waiting for restoration $0 for snapshot $1",
        restoration_id, snapshot_id);
  }

  master::ListSnapshotRestorationsResponsePB list_resp;
  RETURN_NOT_OK(master_->snapshot_coordinator().ListRestorations(
      restoration_id, snapshot_id, &list_resp));

  SCHECK_FORMAT(
      list_resp.restorations_size() == 1, IllegalState,
      "Expected 1 restoration entry for $0 but got $1",
      restoration_id, list_resp.restorations_size());

  const auto state = list_resp.restorations(0).entry().state();
  if (state == SysSnapshotEntryPB::RESTORED) {
    if (!FLAGS_TEST_xcluster_failover_fail_restore_namespace_id.empty() &&
        FLAGS_TEST_xcluster_failover_fail_restore_namespace_id == namespace_id) {
      return STATUS_FORMAT(
          IllegalState, "Simulated failure restoring namespace $0", namespace_id);
    }
    LOG_WITH_PREFIX(INFO)
        << "Restoration " << restoration_id << " for snapshot " << snapshot_id
        << " completed for namespace " << namespace_id;
    ++current_restoration_index_;
    if (current_restoration_index_ >= failovers_.size()) {
      ScheduleNextStep(
          std::bind(&XClusterFailoverTask::DeleteReplicationStep, this),
          "DeleteReplication");
    } else {
      ScheduleNextStep(
          std::bind(&XClusterFailoverTask::PollForCurrentRestoration, this),
          "PollForCurrentRestoration");
    }
    return Status::OK();
  }

  if (state == SysSnapshotEntryPB::RESTORING || state == SysSnapshotEntryPB::CREATING) {
    ScheduleNextStepWithDelay(
        std::bind(&XClusterFailoverTask::PollForCurrentRestoration, this),
        "PollForCurrentRestoration",
        MonoDelta::FromMilliseconds(FLAGS_xcluster_failover_restoration_poll_interval_ms));
    return Status::OK();
  }

  SCHECK(
      state == SysSnapshotEntryPB::FAILED, IllegalState,
      Format("xCluster failover restoration $0 for snapshot $1 returned unexpected state $2",
             restoration_id, snapshot_id, state));

  return STATUS_FORMAT(
      IllegalState,
      "xCluster failover restoration $0 for snapshot $1 failed (state: $2)",
      restoration_id, snapshot_id, state);
}

Status XClusterFailoverTask::DeleteReplicationStep() {
  TEST_PAUSE_IF_FLAG(TEST_pause_xcluster_failover_before_delete_replication);

  if (PREDICT_FALSE(FLAGS_TEST_xcluster_failover_fail_delete_replication)) {
    return STATUS_FORMAT(
        InternalError,
        "Simulated DeleteUniverseReplication failure for replication group $0",
        replication_group_id_);
  }
  // No OID bump is required because failover tears down the entire replication group, so
  // we no longer keep the target namespaces in a state where they need OID reservation.
  master::DeleteUniverseReplicationResponsePB delete_resp;
  auto delete_status = target_manager_.DeleteUniverseReplication(
      replication_group_id_,
      /*ignore_errors=*/true,
      /*skip_producer_stream_deletion=*/true,
      &delete_resp,
      epoch_,
      /*source_namespace_id_to_oid_to_bump_above=*/{});

  if (!delete_status.ok() || delete_resp.has_error()) {
    auto status = !delete_status.ok() ? delete_status : ResponseStatus(delete_resp);
    // Deletion is required to exit target read-only mode. We return a failure so
    // that the client can retry the failover.
    return status.CloneAndPrepend(Format(
        "DeleteUniverseReplication failed for replication group $0", replication_group_id_));
  }

  LOG_WITH_PREFIX(INFO) << "Deleted xCluster replication group " << replication_group_id_;
  ScheduleNextStep(
      std::bind(&XClusterFailoverTask::CleanupSnapshotsStep, this),
      "CleanupSnapshots");
  return Status::OK();
}

Status XClusterFailoverTask::CleanupSnapshotsStep() {
  TryCleanupSnapshots();
  Complete();
  return Status::OK();
}

void XClusterFailoverTask::TryCleanupSnapshots() {
  for (const auto& failover : failovers_) {
    if (failover.snapshot_id.IsNil()) {
      continue;
    }
    const auto& snapshot_id = failover.snapshot_id;
    auto delete_deadline =
        CoarseMonoClock::now() +
        MonoDelta::FromSeconds(FLAGS_xcluster_failover_snapshot_delete_retry_secs);
    auto delete_status = master_->snapshot_coordinator().Delete(
        snapshot_id, epoch_.leader_term, delete_deadline);
    if (!delete_status.ok()) {
      LOG_WITH_PREFIX(WARNING)
          << "Failed to delete failover snapshot " << snapshot_id << ": " << delete_status
          << ". It will be auto-deleted after retention ("
          << FLAGS_xcluster_failover_snapshot_retention_hours << " hr).";
    }
  }
}

void XClusterFailoverTask::TaskCompleted(const Status& status) {
  master_->tablet_split_manager().ReenableSplittingFor(kXClusterFailoverFeatureName);

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING)
        << "Failover task failed (" << status << "), cleaning up snapshot state.";
    TryCleanupSnapshots();

    auto replication_info =
        master_->catalog_manager_impl()->GetUniverseReplication(replication_group_id_);
    if (replication_info) {
      auto repl_info_lock = replication_info->LockForWrite();
      repl_info_lock.mutable_data()->pb.set_failover_in_progress(false);
      StatusToPB(
          status, repl_info_lock.mutable_data()->pb.mutable_last_failover_completion_status());
      auto upsert_status =
          master_->catalog_manager_impl()->sys_catalog()->Upsert(epoch_, replication_info.get());
      if (upsert_status.ok()) {
        repl_info_lock.Commit();
      } else {
        LOG_WITH_PREFIX(WARNING)
            << "Failed to persist failover failure status: " << upsert_status;
      }
    }
  }
  MultiStepMonitoredTask::TaskCompleted(status);
}

} // namespace yb::master
