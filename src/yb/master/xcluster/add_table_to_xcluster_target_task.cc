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

#include "yb/master/xcluster/add_table_to_xcluster_target_task.h"

#include "yb/common/xcluster_util.h"
#include "yb/client/xcluster_client.h"
#include "yb/master/catalog_manager.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/rpc/messenger.h"
#include "yb/util/logging.h"
#include "yb/util/sync_point.h"

DEFINE_test_flag(bool, xcluster_fail_table_create_during_bootstrap, false,
    "Fail the table or index creation during xcluster bootstrap stage.");

#define SCHEDULE_WITH_DELAY(task, ...) \
  ScheduleNextStepWithDelay( \
      std::bind(&AddTableToXClusterTargetTask::task, this, ##__VA_ARGS__), #task, kScheduleDelay);

#define SCHEDULE(task, ...) \
  ScheduleNextStep(std::bind(&AddTableToXClusterTargetTask::task, this, ##__VA_ARGS__), #task);

using namespace std::placeholders;

namespace yb::master {

namespace {
const MonoDelta kScheduleDelay = MonoDelta::FromMilliseconds(200);
}

AddTableToXClusterTargetTask::AddTableToXClusterTargetTask(
    scoped_refptr<UniverseReplicationInfo> universe, CatalogManager& catalog_manager,
    rpc::Messenger& messenger, TableInfoPtr table_info, const LeaderEpoch& epoch)
    : PostTabletCreateTaskBase(
          catalog_manager, *catalog_manager.AsyncTaskPool(), messenger, std::move(table_info),
          std::move(epoch)),
      universe_(universe),
      xcluster_manager_(*catalog_manager.GetXClusterManager()) {
  is_db_scoped_ = universe_->IsDbScoped();
}

std::string AddTableToXClusterTargetTask::description() const {
  return Format("AddTableToXClusterTargetTask [$0]", table_info_->id());
}

Status AddTableToXClusterTargetTask::FirstStep() {
  auto universe_l = universe_->LockForRead();
  auto& universe_pb = universe_l->pb;

  auto table_l = table_info_->LockForRead();

  if (!VERIFY_RESULT(
          ShouldAddTableToReplicationGroup(*universe_, *table_info_, catalog_manager_))) {
    LOG_WITH_PREFIX(INFO) << "Table " << table_info_->ToString()
                          << " does not need to be added to xCluster universe replication";
    RETURN_NOT_OK(CleanupAndComplete());
    return Status::OK();
  }

  SCHECK(
      !FLAGS_TEST_xcluster_fail_table_create_during_bootstrap, IllegalState,
      "FLAGS_TEST_xcluster_fail_table_create_during_bootstrap");

  bool abandon_task = false;
  TEST_SYNC_POINT_CALLBACK(
      "AddTableToXClusterTargetTask::RunInternal::BeforeBootstrap", &abandon_task);
  if (abandon_task) {
    LOG_WITH_PREFIX(WARNING) << "Task will be stuck";
    // Just exit without scheduling work or completing the task.
    return Status::OK();
  }

  auto callback = [this](client::BootstrapProducerResult bootstrap_result) {
    SCHEDULE(AddTableToReplicationGroup, std::move(bootstrap_result));
  };

  if (!is_db_scoped_) {
    auto xcluster_rpc = VERIFY_RESULT(
        universe_->GetOrCreateXClusterRpcTasks(universe_pb.producer_master_addresses()));
    return xcluster_rpc->client()->BootstrapProducer(
        YQLDatabase::YQL_DATABASE_PGSQL, table_info_->namespace_name(),
        {table_info_->pgschema_name()}, {table_info_->name()}, std::move(callback));
  }

  const auto producer_namespace_id =
      VERIFY_RESULT(GetProducerNamespaceId(*universe_, table_info_->namespace_id()));

  // We need to keep the client alive until the callback is invoked.
  remote_client_ = VERIFY_RESULT(GetXClusterRemoteClientHolder(*universe_));

  if (!table_l->pb.xcluster_source_table_id().empty()) {
    // We know the producer table id. We can directly get the checkpoint info.
    LOG(INFO) << "Using xCluster source table id " << table_l->pb.xcluster_source_table_id()
              << " for table " << table_info_->ToString() << " in namespace "
              << table_info_->namespace_name() << " in universe " << universe_->id();
    return remote_client_->GetXClusterClient().GetXClusterTableCheckpointInfos(
        universe_->ReplicationGroupId(), producer_namespace_id,
        {table_l->pb.xcluster_source_table_id()}, std::move(callback));
  }

  return remote_client_->GetXClusterClient().GetXClusterTableCheckpointInfos(
      universe_->ReplicationGroupId(), producer_namespace_id, {table_info_->name()},
      {table_info_->pgschema_name()}, std::move(callback));
}

Status AddTableToXClusterTargetTask::AddTableToReplicationGroup(
    client::BootstrapProducerResult bootstrap_result) {
  const auto& replication_group_id = universe_->ReplicationGroupId();

  SCHECK_EC_FORMAT(
      bootstrap_result, InvalidArgument, MasterError(MasterErrorPB::INVALID_REQUEST),
      "Failed to bootstrap table on the source universe of xCluster replication group $0: $1",
      replication_group_id, bootstrap_result.status().ToString());

  auto& [producer_table_ids, bootstrap_ids, bootstrap_time] = *bootstrap_result;

  CHECK_EQ(producer_table_ids.size(), 1);
  CHECK_EQ(bootstrap_ids.size(), 1);
  if (is_db_scoped_) {
    // With Db scoped replication we do not require the bootstrap time.
    // xCluster streams do not replicate data produced by index backfill. So, both source and
    // target universe have to run their own backfill jobs.
    //
    // In non Db scoped replication we checkpoint the source index at an arbitrary time when the
    // create index DDL is executed on the target by the user. Only data after this time will be
    // replicated by xcluster stream and the target side backfill job will populate the data written
    // before it. We need to for xCluster safe time (which includes the base table) to advance to
    // the bootstrap time to ensure the base table has all the data before we start the backfill
    // job.
    //
    // In Db scoped replication the DDL handler which issues the create index DDL waits for the
    // xCluster safe time to advance upto the DDL commit time before executing it. This time is
    // guaranteed to be higher than the backfill time of the source universe since index creation
    // waits for the backfill job to finish.
    //
    // We set to kMin here and later update it to GetXClusterSafeTimeWithoutDdlQueue.
    bootstrap_time = HybridTime::kMin;
  } else {
    SCHECK(
        !bootstrap_time.is_special(), IllegalState, "xCluster Bootstrap time is not valid $0",
        bootstrap_time.ToString());
  }

  bootstrap_time_ = bootstrap_time;

  auto& producer_table_id = producer_table_ids[0];
  auto& bootstrap_id = bootstrap_ids[0];
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "Adding table to xcluster universe replication "
                                 << replication_group_id << " with bootstrap_id:" << bootstrap_id
                                 << ", bootstrap_time:" << bootstrap_time_
                                 << " and producer_table_id:" << producer_table_id;
  AlterUniverseReplicationRequestPB req;
  AlterUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id.ToString());
  req.add_producer_table_ids_to_add(producer_table_id);
  req.add_producer_bootstrap_ids_to_add(bootstrap_id);
  RETURN_NOT_OK(xcluster_manager_.AlterUniverseReplication(&req, &resp, nullptr /* rpc */, epoch_));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  SCHEDULE_WITH_DELAY(WaitForSetupUniverseReplicationToFinish);
  return Status::OK();
}

Status AddTableToXClusterTargetTask::WaitForSetupUniverseReplicationToFinish() {
  // Skip the health checks since we will wait for the safe time to advance in the next step.
  auto operation_result = VERIFY_RESULT(xcluster_manager_.IsSetupUniverseReplicationDone(
      xcluster::GetAlterReplicationGroupId(universe_->ReplicationGroupId()),
      /*skip_health_check=*/true));

  if (!operation_result.done()) {
    VLOG_WITH_PREFIX(2) << "Waiting for setup universe replication to finish";
    // If this takes too long the table creation will timeout and abort the task.
    SCHEDULE_WITH_DELAY(WaitForSetupUniverseReplicationToFinish);
    return Status::OK();
  }

  RETURN_NOT_OK(operation_result.status());

  SCHEDULE(RefreshAndGetXClusterSafeTime);
  return Status::OK();
}

Result<std::optional<HybridTime>>
AddTableToXClusterTargetTask::GetXClusterSafeTimeWithoutDdlQueue() {
  const auto namespace_id = table_info_->namespace_id();

  auto safe_time_res = xcluster_manager_.GetXClusterSafeTimeForNamespace(
      epoch_, namespace_id, XClusterSafeTimeFilter::DDL_QUEUE);
  if (!safe_time_res) {
    if (!safe_time_res.status().IsNotFound()) {
      return safe_time_res.status();
    }
    VLOG_WITH_PREFIX(2) << "Namespace " << namespace_id
                        << " is no longer part of any xCluster replication";
    return std::nullopt;
  }

  if (!safe_time_res->is_valid()) {
    // If this is the first table in the namespace then the safe time will be invalid. Which means
    // it is not yet valid to scan any data in the namespace. Since we only care about progression
    // of the safe time and dont read any data we can safely use Min HT.
    // This case will not occur if we have DDL replication, since we never drop the DDL_QUEUE table.
    return HybridTime::kMin;
  }

  SCHECK(
      !safe_time_res->is_special(), IllegalState, "Invalid safe time $0 for namespace $1",
      *safe_time_res, namespace_id);
  return *safe_time_res;
}

Status AddTableToXClusterTargetTask::RefreshAndGetXClusterSafeTime() {
  // Force a refresh of the xCluster safe time map so that it accounts for all tables under
  // replication.
  RETURN_NOT_OK(xcluster_manager_.RefreshXClusterSafeTimeMap(epoch_));
  auto initial_safe_time = VERIFY_RESULT(GetXClusterSafeTimeWithoutDdlQueue());
  if (!initial_safe_time) {
    RETURN_NOT_OK(CleanupAndComplete());
    return Status::OK();
  }

  initial_xcluster_safe_time_ = *initial_safe_time;
  initial_xcluster_safe_time_.MakeAtLeast(bootstrap_time_);

  // Wait for the xCluster safe time to advance beyond the initial value. This ensures all tables
  // under replication are part of the safe time computation.
  SCHEDULE_WITH_DELAY(WaitForXClusterSafeTimeCaughtUp);
  return Status::OK();
}

Status AddTableToXClusterTargetTask::WaitForXClusterSafeTimeCaughtUp() {
  auto ht = VERIFY_RESULT(GetXClusterSafeTimeWithoutDdlQueue());
  if (!ht) {
    // The namespace is no longer part of any xCluster replication.
    RETURN_NOT_OK(CleanupAndComplete());
    return Status::OK();
  }

  if (*ht <= initial_xcluster_safe_time_) {
    YB_LOG_EVERY_N_SECS(WARNING, 10) << LogPrefix() << "Waiting for xCluster safe time " << *ht
                                     << " to advance beyond " << initial_xcluster_safe_time_;
    // If this takes too long the table creation will timeout and abort the task.
    SCHEDULE_WITH_DELAY(WaitForXClusterSafeTimeCaughtUp);
    return Status::OK();
  }

  LOG(INFO) << "Table " << table_info_->ToString()
            << " successfully added to xCluster universe replication";

  RETURN_NOT_OK(CleanupAndComplete());
  return Status::OK();
}

Status AddTableToXClusterTargetTask::CleanupAndComplete() {
  // Ensure that we clean up the xcluster_source_table_id field.
  RETURN_NOT_OK(xcluster_manager_.ClearXClusterSourceTableId(table_info_, epoch_));

  Complete();
  return Status::OK();
}

}  // namespace yb::master
