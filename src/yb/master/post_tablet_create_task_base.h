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

#pragma once

#include "yb/master/catalog_entity_tasks.h"

namespace yb::master {

// Tasks that run on a newly created table (PREPARING state) after all its tablets have been
// created and marked as RUNNING. Once all tasks of this type complete, the table will be marked as
// RUNNING, which completes the table creation workflow.
// - Check MultiStepMonitoredTask for information about scheduling.
class PostTabletCreateTaskBase : public MultiStepTableTaskBase {
 public:
  // Start all the tasks at once. Once the last task completes it will transition the table to
  // RUNNING state if healthy.
  // NOTE: Do not call this directly. Add your task to
  // `CatalogManager::SchedulePostTabletCreationTasks`.
  static Status StartTasks(
      const std::vector<std::shared_ptr<PostTabletCreateTaskBase>>& table_creation_tasks,
      CatalogManager* catalog_manager, TableInfoPtr table_info, const LeaderEpoch& epoch);

 protected:
  explicit PostTabletCreateTaskBase(
      CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
      TableInfoPtr table_info, const LeaderEpoch& epoch);

  virtual ~PostTabletCreateTaskBase() = default;

 private:
  void Start() override { MultiStepMonitoredTask::Start(); }  // Marking private.
  Status ValidateRunnable() override;
  void TaskCompleted(const Status& s) override;
};

// A simple task that will directly complete. This is used to transition the table to RUNNING
// state in a async manner.
class MarkTableAsRunningTask : public PostTabletCreateTaskBase {
 public:
  MarkTableAsRunningTask(
      CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
      TableInfoPtr table_info, const LeaderEpoch& epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kMarkTableAsRunning;
  }

  std::string type_name() const override { return "Marking table as RUNNNING"; }

  std::string description() const override;

 private:
  Status FirstStep() override;
};

}  // namespace yb::master
