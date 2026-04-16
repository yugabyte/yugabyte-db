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

#include "yb/master/xcluster/add_index_to_bidirectional_xcluster_target_task.h"

#include "yb/client/client.h"
#include "yb/client/table_info.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/xcluster_util.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_manager_if.h"

#include "yb/util/is_operation_done_result.h"

namespace yb::master {

#define SCHEDULE_WITH_DELAY_AND_RETURN(task, ...) \
  SCHECK_LT(CoarseMonoClock::Now(), deadline_, TimedOut, Format("$0 Timed out", LogPrefix())); \
  ScheduleNextStepWithDelay( \
      std::bind(&AddBiDirectionalIndexToXClusterTargetTask::task, this, ##__VA_ARGS__), #task, \
      kScheduleDelay); \
  return Status::OK()

#define SCHEDULE_AND_RETURN(task, ...) \
  ScheduleNextStep( \
      std::bind(&AddBiDirectionalIndexToXClusterTargetTask::task, this, ##__VA_ARGS__), #task); \
  return Status::OK()

namespace {
const MonoDelta kScheduleDelay = MonoDelta::FromMilliseconds(200);
}

AddBiDirectionalIndexToXClusterTargetTask::AddBiDirectionalIndexToXClusterTargetTask(
    TableInfoPtr index_table_info, scoped_refptr<UniverseReplicationInfo> universe,
    std::shared_ptr<client::XClusterRemoteClientHolder> remote_client, Master& master,
    const LeaderEpoch& epoch, CoarseTimePoint deadline)
    : MultiStepTableTaskBase(
          *master.catalog_manager_impl(), *master.catalog_manager()->AsyncTaskPool(),
          *master.messenger(), std::move(index_table_info), epoch),
      universe_(universe),
      remote_client_(std::move(remote_client)),
      xcluster_manager_(*master.catalog_manager()->GetXClusterManager()),
      deadline_(deadline) {}

std::string AddBiDirectionalIndexToXClusterTargetTask::description() const {
  return Format("AddBiDirectionalIndexToXClusterTargetTask [$0]", table_info_->id());
}

Status AddBiDirectionalIndexToXClusterTargetTask::FirstStep() {
  SCHECK_FORMAT(table_info_->is_index(), IllegalState, "$0 is not an index", table_info_->id());

  // Skip if the table has already been added to this replication group.
  if (VERIFY_RESULT(HasTable(*universe_, *table_info_, catalog_manager_))) {
    LOG_WITH_PREFIX(INFO) << "Table " << table_info_->ToString()
                          << " is already part of xCluster universe replication "
                          << universe_->ToString();
    Complete();
    return Status::OK();
  }

  return GetSourceIndexTableId();
}

Status AddBiDirectionalIndexToXClusterTargetTask::GetSourceIndexTableId() {
  auto source_tables_resp = VERIFY_RESULT(
      remote_client_->GetYbClient().ListTables(table_info_->name(), table_info_->namespace_name()));

  // Filter on the pgschema_name.
  const auto pgschema_name = table_info_->pgschema_name();
  auto source_table = std::find_if(
      source_tables_resp.tables().begin(), source_tables_resp.tables().end(),
      [&pgschema_name](const auto& table) { return table.pgschema_name() == pgschema_name; });

  if (source_table == source_tables_resp.tables().end() ||
      source_table->state() != SysTablesEntryPB_State_RUNNING) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 10)
        << "Waiting for index to be created on other universe";
    SCHEDULE_WITH_DELAY_AND_RETURN(GetSourceIndexTableId);
  }

  SCHECK(
      source_table->has_indexed_table_id() && !source_table->indexed_table_id().empty(),
      IllegalState, Format("Source table $0 is not an index", source_table->id()));

  source_index_table_id_ = source_table->id();
  source_indexed_table_id_ = source_table->indexed_table_id();

  SCHEDULE_AND_RETURN(WaitForBackfillIndexToStartOnSource);
}

Status AddBiDirectionalIndexToXClusterTargetTask::WaitForBackfillIndexToStartOnSource() {
  SCHECK(!source_index_table_id_.empty(), IllegalState, "Source index table id is empty");
  SCHECK(!source_indexed_table_id_.empty(), IllegalState, "Source indexed table id is empty");

  auto backfill_started = VERIFY_RESULT(remote_client_->GetYbClient().IsBackfillIndexStarted(
      source_index_table_id_, source_indexed_table_id_, deadline_));

  if (!backfill_started) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 10)
        << "Waiting for backfill of index " << source_index_table_id_
        << " to start on other universe";
    SCHEDULE_WITH_DELAY_AND_RETURN(WaitForBackfillIndexToStartOnSource);
  }

  if (table_info_->colocated()) {
    // The parent tablet is already part of xCluster replication.
    Complete();
    return Status::OK();
  }

  SCHEDULE_AND_RETURN(GetSourceIndexStreamId);
}

Status AddBiDirectionalIndexToXClusterTargetTask::GetSourceIndexStreamId() {
  RSTATUS_DCHECK(!source_index_table_id_.empty(), IllegalState, "Source index table id is empty");

  auto stream_ids =
      VERIFY_RESULT(remote_client_->GetXClusterClient().GetXClusterStreams(source_index_table_id_));

  SCHECK_EQ(
      stream_ids.size(), 1, IllegalState,
      Format("Expected exactly one stream for source table $0", source_index_table_id_));

  auto& stream_id = stream_ids.front();

  SCHEDULE_AND_RETURN(AddIndexToReplicationGroup, stream_id);
}

Status AddBiDirectionalIndexToXClusterTargetTask::AddIndexToReplicationGroup(
    const xrepl::StreamId& stream_id) {
  RETURN_NOT_OK(xcluster_manager_.AddTableToReplicationGroup(
      universe_->ReplicationGroupId(), source_index_table_id_, stream_id,
      /*target_table_id=*/std::nullopt, epoch_));

  SCHEDULE_WITH_DELAY_AND_RETURN(WaitForSetupReplication);
}

Status AddBiDirectionalIndexToXClusterTargetTask::WaitForSetupReplication() {
  // Perform health checks so that the Create Index DDL fails when replication is not healthy.
  auto operation_result = VERIFY_RESULT(xcluster_manager_.IsSetupUniverseReplicationDone(
      xcluster::GetAlterReplicationGroupId(universe_->ReplicationGroupId()),
      /*skip_health_check=*/false));

  if (!operation_result.done()) {
    VLOG_WITH_PREFIX(2) << "Waiting for setup universe replication to finish";
    // If this takes too long the table creation will timeout and abort the task.
    SCHEDULE_WITH_DELAY_AND_RETURN(WaitForSetupReplication);
  }

  RETURN_NOT_OK(operation_result.status());

  Complete();

  return Status::OK();
}

}  // namespace yb::master
