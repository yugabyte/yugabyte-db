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

#include "yb/master/catalog_manager.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/rpc/messenger.h"
#include "yb/util/logging.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"

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
    CatalogManager& catalog_manager, rpc::Messenger& messenger, TableInfoPtr table_info,
    const LeaderEpoch& epoch)
    : PostTabletCreateTaskBase(
          catalog_manager, *catalog_manager.AsyncTaskPool(), messenger, std::move(table_info),
          std::move(epoch)) {}

std::string AddTableToXClusterTargetTask::description() const {
  return Format("AddTableToXClusterTargetTask [$0]", table_info_->id());
}

Status AddTableToXClusterTargetTask::FirstStep() {
  auto stream_ids = catalog_manager_.GetXClusterConsumerStreamIdsForTable(table_info_->id());
  if (!stream_ids.empty()) {
    LOG_WITH_PREFIX(INFO) << "Table is already part of xcluster replication "
                          << yb::ToString(stream_ids);
    Complete();
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

  replication_group_id_ =
      VERIFY_RESULT(catalog_manager_.GetIndexesTableReplicationGroup(*table_info_));
  DCHECK(!replication_group_id_.empty());

  return catalog_manager_.BootstrapTable(
      replication_group_id_, *table_info_,
      std::bind(&AddTableToXClusterTargetTask::BootstrapTableCallback, shared_from(this), _1));
}

Status AddTableToXClusterTargetTask::BootstrapTableCallback(
    client::BootstrapProducerResult bootstrap_result) {
  auto [producer_table_ids, bootstrap_ids, bootstrap_time] =
      VERIFY_RESULT(std::move(bootstrap_result));
  CHECK_EQ(producer_table_ids.size(), 1);
  CHECK_EQ(bootstrap_ids.size(), 1);
  SCHECK(!bootstrap_time.is_special(), IllegalState, "Failed to get a valid bootstrap time");
  bootstrap_time_ = bootstrap_time;

  SCHEDULE(AddTableToReplicationGroup, producer_table_ids[0], bootstrap_ids[0]);
  return Status::OK();
}

Status AddTableToXClusterTargetTask::AddTableToReplicationGroup(
    TableId producer_table_id, std::string bootstrap_id) {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "Adding table to xcluster universe replication "
                                 << replication_group_id_ << " with bootstrap_id:" << bootstrap_id
                                 << ", bootstrap_time:" << bootstrap_time_
                                 << " and producer_table_id:" << producer_table_id;
  AlterUniverseReplicationRequestPB alter_universe_req;
  AlterUniverseReplicationResponsePB alter_universe_resp;
  alter_universe_req.set_replication_group_id(replication_group_id_.ToString());
  alter_universe_req.add_producer_table_ids_to_add(producer_table_id);
  alter_universe_req.add_producer_bootstrap_ids_to_add(bootstrap_id);
  RETURN_NOT_OK(catalog_manager_.AlterUniverseReplication(
      &alter_universe_req, &alter_universe_resp, nullptr /* rpc */));

  if (alter_universe_resp.has_error()) {
    return StatusFromPB(alter_universe_resp.error().status());
  }

  VLOG_WITH_PREFIX(1) << "Waiting for xcluster safe time of namespace "
                      << table_info_->namespace_id() << " to get past bootstrap_time "
                      << bootstrap_time_;

  SCHEDULE_WITH_DELAY(WaitForSetupUniverseReplicationToFinish);
  return Status::OK();
}

Status AddTableToXClusterTargetTask::WaitForSetupUniverseReplicationToFinish() {
  IsSetupUniverseReplicationDoneRequestPB check_req;
  IsSetupUniverseReplicationDoneResponsePB check_resp;
  check_req.set_replication_group_id(replication_group_id_.ToString());
  auto status = catalog_manager_.IsSetupUniverseReplicationDone(
      &check_req, &check_resp, /* RpcContext */ nullptr);
  if (status.ok() && check_resp.has_error()) {
    status = StatusFromPB(check_resp.error().status());
  }

  if (!status.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << LogPrefix() << "Failed to check if setup universe replication is done: " << status;
    SCHEDULE_WITH_DELAY(WaitForSetupUniverseReplicationToFinish);
    return Status::OK();
  }

  if (!check_resp.done()) {
    VLOG_WITH_PREFIX(2) << "Waiting for setup universe replication to finish";
    // If this takes too long the table creation will timeout and abort the task.
    SCHEDULE_WITH_DELAY(WaitForSetupUniverseReplicationToFinish);
    return Status::OK();
  }

  if (check_resp.has_replication_error()) {
    RETURN_NOT_OK(StatusFromPB(check_resp.replication_error()));
  }

  SCHEDULE(RefreshAndGetXClusterSafeTime);
  return Status::OK();
}

Status AddTableToXClusterTargetTask::RefreshAndGetXClusterSafeTime() {
  CHECK(bootstrap_time_.is_valid());
  // Force a refresh of the xCluster safe time map so that it accounts for all tables under
  // replication.
  auto namespace_id = table_info_->namespace_id();
  auto initial_safe_time = VERIFY_RESULT(
      catalog_manager_.GetXClusterManager()->RefreshAndGetXClusterNamespaceToSafeTimeMap(
          catalog_manager_.GetLeaderEpochInternal()));
  if (!initial_safe_time.contains(namespace_id)) {
    VLOG_WITH_PREFIX(2) << "Namespace " << namespace_id
                        << " is no longer part of any xCluster replication";
    Complete();
    return Status::OK();
  }

  initial_xcluster_safe_time_ = initial_safe_time[namespace_id];
  initial_xcluster_safe_time_.MakeAtLeast(bootstrap_time_);

  // Wait for the xCluster safe time to advance beyond the initial value. This ensures all tables
  // under replication are part of the safe time computation.
  SCHEDULE_WITH_DELAY(WaitForXClusterSafeTimeCaughtUp);
  return Status::OK();
}

Status AddTableToXClusterTargetTask::WaitForXClusterSafeTimeCaughtUp() {
  if (initial_xcluster_safe_time_.is_valid()) {
    // TODO: Handle the case when replication was dropped.
    auto ht = VERIFY_RESULT(
        catalog_manager_.GetXClusterManager()->GetXClusterSafeTime(table_info_->namespace_id()));

    auto caught_up = ht > initial_xcluster_safe_time_;
    if (!caught_up) {
      YB_LOG_EVERY_N_SECS(WARNING, 10) << LogPrefix() << "Waiting for xCluster safe time " << ht
                                       << " to advance beyond " << initial_xcluster_safe_time_;
      // If this takes too long the table creation will timeout and abort the task.
      SCHEDULE_WITH_DELAY(WaitForXClusterSafeTimeCaughtUp);
      return Status::OK();
    }
  }

  LOG(INFO) << "Table " << table_info_->ToString()
            << " successfully added to xCluster universe replication";

  Complete();
  return Status::OK();
}

}  // namespace yb::master
