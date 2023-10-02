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

#pragma once

#include <string>

#include "yb/cdc/cdc_types.h"
#include "yb/client/client_fwd.h"
#include "yb/common/hybrid_time.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/server/monitored_task.h"

namespace yb::master {
class AddTableToXClusterTask : public server::RunnableMonitoredTask {
 public:
  AddTableToXClusterTask(
      CatalogManager* catalog_manager, TableInfoPtr table_info, LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToXClusterReplication;
  }
  // Abort this task and return its value before it was successfully aborted. If the task entered
  // a different terminal state before we were able to abort it, return that state.
  server::MonitoredTaskState AbortAndReturnPrevState(const Status& status) override;

  // Task Type Identifier.
  std::string type_name() const override { return "Add Table to xCluster replication"; }

  // Task description.
  std::string description() const override;

  Status Run() override;

 private:
  std::string LogPrefix() { return Format("$0: ", description()); }

  void RunInternal();
  void BootstrapTableCallback(client::BootstrapProducerResult bootstrap_result);
  void AddTableToReplicationGroup(TableId producer_table_id, std::string bootstrap_id);
  void WaitForSetupUniverseReplicationToFinish();
  void RefreshAndGetXClusterSafeTime();
  void WaitForXClusterSafeTimeCaughtUp();
  void CompleteTableCreation();

  // Methods related to scheduling and task completion.
  // If the task needs to run with a delay we schedule on the reactor. If the task needs to run
  // immediately or waits on locks we schedule on the background thread.
  void ScheduleOnReactor(std::function<void()> task, std::string task_description);
  void ScheduleOnAsyncTaskPool(std::function<void()> task, std::string task_description);
  void TransitionToRunningAndExecute(std::function<void()> task, std::string task_description);
  void AbortReactorTaskIfScheduled();
  void FinishTask(const Status& s);
  bool TransitionState(server::MonitoredTaskState new_state);

  CatalogManager* catalog_manager_;
  TableInfoPtr table_info_;
  std::mutex schedule_task_mutex_;
  rpc::ScheduledTaskId reactor_task_id_ GUARDED_BY(schedule_task_mutex_) = rpc::kInvalidTaskId;
  HybridTime bootstrap_time_ = HybridTime::kInvalid;
  HybridTime initial_xcluster_safe_time_ = HybridTime::kInvalid;
  cdc::ReplicationGroupId replication_group_id_;
  LeaderEpoch epoch_;
};
}  // namespace yb::master
