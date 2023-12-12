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

#include "yb/master/xcluster/add_table_to_xcluster_task.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/rpc/messenger.h"
#include "yb/util/source_location.h"
#include "yb/util/logging.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, xcluster_fail_table_create_during_bootstrap, false,
    "Fail the table or index creation during xcluster bootstrap stage.");

#define FAIL_TASK_AND_RETURN_IF(cond, status) \
  do { \
    if ((cond)) { \
      AbortAndReturnPrevState((status)); \
      return; \
    } \
  } while (false)

#define FAIL_TASK_AND_RETURN_IF_NOT_OK(status) \
  FAIL_TASK_AND_RETURN_IF(!status.ok(), MoveStatus(status))

#define VERIFY_RESULT_AND_FAIL_TASK(expr) \
  RESULT_CHECKER_HELPER(expr, FAIL_TASK_AND_RETURN_IF_NOT_OK(__result))

#define SCHEDULE_ON_REACTOR_AND_RETURN(task, ...) \
  ScheduleOnReactor(std::bind(&AddTableToXClusterTask::task, this, ##__VA_ARGS__), #task); \
  return

#define SCHEDULE_ON_ASYNC_POOL_AND_RETURN(task, ...) \
  ScheduleOnAsyncTaskPool(std::bind(&AddTableToXClusterTask::task, this, ##__VA_ARGS__), #task); \
  return

using namespace std::placeholders;

namespace yb::master {

const auto kDefaultReactorDelay = MonoDelta::FromMilliseconds(200);

AddTableToXClusterTask::AddTableToXClusterTask(
    CatalogManager* catalog_manager, TableInfoPtr table_info, LeaderEpoch epoch)
    : catalog_manager_(catalog_manager), table_info_(table_info), epoch_(std::move(epoch)) {}

std::string AddTableToXClusterTask::description() const {
  return Format("AddTableToXClusterTask [$0]", table_info_->id());
}

Status AddTableToXClusterTask::Run() {
  TransitionToRunningAndExecute([this]() { RunInternal(); }, "RunInternal");
  return Status::OK();
}

void AddTableToXClusterTask::RunInternal() {
  {
    TRACE("Locking table");
    auto l = table_info_->LockForRead();
    FAIL_TASK_AND_RETURN_IF(
        !l->IsPreparing(),
        STATUS(
            IllegalState,
            "Table $0 should be in PREPARING state to be added to xCluster replication.",
            table_info_->ToString()));
  }

  auto stream_ids = catalog_manager_->GetXClusterConsumerStreamIdsForTable(table_info_->id());
  if (!stream_ids.empty()) {
    LOG_WITH_PREFIX(INFO) << "Table is already part of xcluster replication "
                          << yb::ToString(stream_ids);
    CompleteTableCreation();
    return;
  }

  FAIL_TASK_AND_RETURN_IF(
      FLAGS_TEST_xcluster_fail_table_create_during_bootstrap,
      STATUS(IllegalState, "FLAGS_TEST_xcluster_fail_table_create_during_bootstrap"));

  bool stuck_add_table_to_xcluster = false;
  TEST_SYNC_POINT_CALLBACK(
      "AddTableToXClusterTask::RunInternal::BeforeBootstrap", &stuck_add_table_to_xcluster);
  if (stuck_add_table_to_xcluster) {
    LOG_WITH_PREFIX(WARNING) << "Task will be stuck";
    // Just exit without scheduling work or completing the task.
    return;
  }

  replication_group_id_ =
      VERIFY_RESULT_AND_FAIL_TASK(catalog_manager_->GetIndexesTableReplicationGroup(*table_info_));
  DCHECK(!replication_group_id_.empty());

  FAIL_TASK_AND_RETURN_IF_NOT_OK(catalog_manager_->BootstrapTable(
      replication_group_id_,
      *table_info_,
      std::bind(&AddTableToXClusterTask::BootstrapTableCallback, shared_from(this), _1)));
}

void AddTableToXClusterTask::BootstrapTableCallback(
    client::BootstrapProducerResult bootstrap_result) {
  auto [producer_table_ids, bootstrap_ids, bootstrap_time] =
      VERIFY_RESULT_AND_FAIL_TASK(std::move(bootstrap_result));
  CHECK_EQ(producer_table_ids.size(), 1);
  CHECK_EQ(bootstrap_ids.size(), 1);
  FAIL_TASK_AND_RETURN_IF(
      bootstrap_time.is_special(), STATUS(IllegalState, "Failed to get a valid bootstrap time"));
  bootstrap_time_ = bootstrap_time;

  SCHEDULE_ON_ASYNC_POOL_AND_RETURN(
      AddTableToReplicationGroup, producer_table_ids[0], bootstrap_ids[0]);
}

void AddTableToXClusterTask::AddTableToReplicationGroup(
    TableId producer_table_id, std::string bootstrap_id) {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "Adding table to xcluster universe replication "
                                 << replication_group_id_ << " with bootstrap_id " << bootstrap_id
                                 << " and bootstrap_time " << bootstrap_time_;
  AlterUniverseReplicationRequestPB alter_universe_req;
  AlterUniverseReplicationResponsePB alter_universe_resp;
  alter_universe_req.set_replication_group_id(replication_group_id_.ToString());
  alter_universe_req.add_producer_table_ids_to_add(producer_table_id);
  alter_universe_req.add_producer_bootstrap_ids_to_add(bootstrap_id);
  FAIL_TASK_AND_RETURN_IF_NOT_OK(catalog_manager_->AlterUniverseReplication(
      &alter_universe_req, &alter_universe_resp, nullptr /* rpc */));

  FAIL_TASK_AND_RETURN_IF(
      alter_universe_resp.has_error(), StatusFromPB(alter_universe_resp.error().status()));

  VLOG_WITH_PREFIX(1) << "Waiting for xcluster safe time of namespace "
                      << table_info_->namespace_id() << " to get past bootstrap_time "
                      << bootstrap_time_;

  SCHEDULE_ON_REACTOR_AND_RETURN(WaitForSetupUniverseReplicationToFinish);
}

void AddTableToXClusterTask::WaitForSetupUniverseReplicationToFinish() {
  IsSetupUniverseReplicationDoneRequestPB check_req;
  IsSetupUniverseReplicationDoneResponsePB check_resp;
  check_req.set_replication_group_id(replication_group_id_.ToString());
  auto status = catalog_manager_->IsSetupUniverseReplicationDone(
      &check_req, &check_resp, /* RpcContext */ nullptr);
  if (status.ok() && check_resp.has_error()) {
    status = StatusFromPB(check_resp.error().status());
  }

  if (!status.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << LogPrefix() << "Failed to check if setup universe replication is done: " << status;
    SCHEDULE_ON_REACTOR_AND_RETURN(WaitForSetupUniverseReplicationToFinish);
  }

  if (check_resp.has_done() && check_resp.done()) {
    status = StatusFromPB(check_resp.replication_error());
  }
  FAIL_TASK_AND_RETURN_IF_NOT_OK(status);

  SCHEDULE_ON_ASYNC_POOL_AND_RETURN(RefreshAndGetXClusterSafeTime);
}

void AddTableToXClusterTask::RefreshAndGetXClusterSafeTime() {
  CHECK(bootstrap_time_.is_valid());
  // Force a refresh of the xCluster safe time map so that it accounts for all tables under
  // replication.
  auto namespace_id = table_info_->namespace_id();
  auto initial_safe_time = VERIFY_RESULT_AND_FAIL_TASK(
      catalog_manager_->GetXClusterManager()->RefreshAndGetXClusterNamespaceToSafeTimeMap(
          catalog_manager_->GetLeaderEpochInternal()));
  if (!initial_safe_time.contains(namespace_id)) {
    // Namespace is no longer part of any xCluster replication.
    CompleteTableCreation();
    return;
  }

  initial_xcluster_safe_time_ = initial_safe_time[namespace_id];
  initial_xcluster_safe_time_.MakeAtLeast(bootstrap_time_);

  // Wait for the xCluster safe time to advance beyond the initial value. This ensures all tables
  // under replication are part of the safe time computation.
  SCHEDULE_ON_REACTOR_AND_RETURN(WaitForXClusterSafeTimeCaughtUp);
}

void AddTableToXClusterTask::WaitForXClusterSafeTimeCaughtUp() {
  if (initial_xcluster_safe_time_.is_valid()) {
    auto ht = VERIFY_RESULT_AND_FAIL_TASK(
        catalog_manager_->GetXClusterManager()->GetXClusterSafeTime(table_info_->namespace_id()));

    auto caught_up = ht > initial_xcluster_safe_time_;
    if (!caught_up) {
      YB_LOG_EVERY_N_SECS(WARNING, 10) << LogPrefix() << "Waiting for xCluster safe time " << ht
                                       << " to advance beyond " << initial_xcluster_safe_time_;
      SCHEDULE_ON_REACTOR_AND_RETURN(WaitForXClusterSafeTimeCaughtUp);
    }
  }

  // CompleteTableCreation requires a lock with wait. Since we cannot wait on the reactor thread,
  // run it in background_tasks_thread_pool .
  SCHEDULE_ON_ASYNC_POOL_AND_RETURN(CompleteTableCreation);
}

void AddTableToXClusterTask::CompleteTableCreation() {
  FAIL_TASK_AND_RETURN_IF_NOT_OK(catalog_manager_->PromoteTableToRunningState(table_info_, epoch_));

  LOG(INFO) << "Table " << table_info_->ToString()
            << " successfully added to xcluster universe replication";

  FinishTask(Status::OK());
  TransitionState(server::MonitoredTaskState::kComplete);
}

void AddTableToXClusterTask::ScheduleOnReactor(
    std::function<void()> task, std::string task_description) {
  std::lock_guard l(schedule_task_mutex_);
  if (!TransitionState(server::MonitoredTaskState::kWaiting)) {
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Task has been aborted";
    return;
  }

  VLOG_WITH_PREFIX(2) << "Scheduling " << task_description << " on reactor";

  auto task_id =
      VERIFY_RESULT_AND_FAIL_TASK(catalog_manager_->master_->messenger()->ScheduleOnReactor(
          std::bind(
              &AddTableToXClusterTask::TransitionToRunningAndExecute, shared_from(this),
              std::move(task), std::move(task_description)),
          kDefaultReactorDelay, SOURCE_LOCATION()));

  VLOG_WITH_PREFIX_AND_FUNC(2) << "Reactor task id: " << task_id;
  reactor_task_id_ = task_id;
}

void AddTableToXClusterTask::ScheduleOnAsyncTaskPool(
    std::function<void()> task, std::string task_description) {
  std::lock_guard l(schedule_task_mutex_);
  if (!TransitionState(server::MonitoredTaskState::kWaiting)) {
    LOG_WITH_PREFIX_AND_FUNC(WARNING) << "Task has been aborted";
    return;
  }
  VLOG_WITH_PREFIX(2) << "Scheduling " << task_description << " on async task pool";

  const auto status = catalog_manager_->async_task_pool_->SubmitFunc(std::bind(
      &AddTableToXClusterTask::TransitionToRunningAndExecute, shared_from(this), std::move(task),
      std::move(task_description)));

  FAIL_TASK_AND_RETURN_IF(
      !status.ok(), status.CloneAndPrepend("Failed to submit task to async task pool"));
}

void AddTableToXClusterTask::TransitionToRunningAndExecute(
    std::function<void()> task, std::string task_description) {
  VLOG_WITH_PREFIX(2) << "Running " << task_description;
  if (!TransitionState(server::MonitoredTaskState::kRunning)) {
    LOG_WITH_PREFIX(WARNING) << "Task has been aborted";
    return;
  }

  {
    std::lock_guard l(schedule_task_mutex_);
    reactor_task_id_ = rpc::kInvalidTaskId;
  }

  task();
}

void AddTableToXClusterTask::FinishTask(const Status& s) {
  if (!s.ok()) {
    table_info_->SetCreateTableErrorStatus(s);
  }

  table_info_->RemoveTask(shared_from_this());
  completion_timestamp_ = MonoTime::Now();
}

void AddTableToXClusterTask::AbortReactorTaskIfScheduled() {
  rpc::ScheduledTaskId reactor_task_id;
  {
    std::lock_guard l(schedule_task_mutex_);
    reactor_task_id = reactor_task_id_;
    reactor_task_id_ = rpc::kInvalidTaskId;
  }

  if (reactor_task_id != rpc::kInvalidTaskId) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Aborting reactor task id: " << reactor_task_id;
    catalog_manager_->master_->messenger()->AbortOnReactor(reactor_task_id);
  }
}

server::MonitoredTaskState AddTableToXClusterTask::AbortAndReturnPrevState(const Status& status) {
  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state, server::MonitoredTaskState::kAborted)) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "Aborted due to: " << status;
      AbortReactorTaskIfScheduled();
      FinishTask(status);
      return old_state;
    }
    old_state = state();
  }
  return old_state;
}

bool AddTableToXClusterTask::TransitionState(server::MonitoredTaskState new_state) {
  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state, new_state)) {
      return true;
    }
    old_state = state();
  }

  return false;
}

}  // namespace yb::master
