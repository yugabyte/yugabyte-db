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

#include "yb/master/post_tablet_create_task_base.h"

#include "yb/master/catalog_manager.h"

using namespace std::placeholders;

namespace yb::master {

namespace {

void MarkTableAsRunningIfHealthy(
    CatalogManager* catalog_manager, TableInfoPtr table_info, const LeaderEpoch& epoch,
    const Status& status) {
  if (!status.ok() || !table_info->GetCreateTableErrorStatus().ok()) {
    return;
  }

  auto promote_status = catalog_manager->PromoteTableToRunningState(table_info, epoch);
  if (!promote_status.ok()) {
    table_info->SetCreateTableErrorStatus(promote_status);
    LOG(WARNING) << "Failed to promote table " << table_info->ToString()
                 << " to RUNNING state: " << promote_status;
  }
}

}  // namespace

PostTabletCreateTaskBase::PostTabletCreateTaskBase(
    CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
    TableInfoPtr table_info, const LeaderEpoch& epoch)
    : MultiStepTableTaskBase(
          catalog_manager, async_task_pool, messenger, std::move(table_info), std::move(epoch)) {}

Status PostTabletCreateTaskBase::ValidateRunnable() {
  RETURN_NOT_OK(MultiStepTableTaskBase::ValidateRunnable());

  SCHECK(
      table_info_->IsPreparing(), IllegalState,
      "Task $0 failed since table is in state $1 when it was expected to be in state $2",
      description(), table_info_->LockForRead()->state_name(),
      SysTablesEntryPB::State_Name(SysTablesEntryPB::PREPARING));
  return Status::OK();
}

Status PostTabletCreateTaskBase::StartTasks(
    const std::vector<std::shared_ptr<PostTabletCreateTaskBase>>& table_creation_tasks,
    CatalogManager* catalog_manager, TableInfoPtr table_info, const LeaderEpoch& epoch) {
  SCHECK(!table_creation_tasks.empty(), IllegalState, "At least one task must be scheduled");

  std::vector<MultiStepMonitoredTask*> tasks;
  for (const auto& task : table_creation_tasks) {
    SCHECK_EQ(
        task->table_info_->id(), table_info->id(), IllegalState,
        "All tasks in group must belong to the same table");
    tasks.emplace_back(task.get());
  }

  return MultiStepMonitoredTask::StartTasks(
      tasks, std::bind(&MarkTableAsRunningIfHealthy, catalog_manager, table_info, epoch, _1));
}

void PostTabletCreateTaskBase::TaskCompleted(const Status& status) {
  if (!status.ok() && table_info_->GetCreateTableErrorStatus().ok()) {
    // There is a small race between the Get and the Set which can be ignored since we never reset a
    // bad status with OK status.
    table_info_->SetCreateTableErrorStatus(status);
  }
}

MarkTableAsRunningTask::MarkTableAsRunningTask(
    CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
    TableInfoPtr table_info, const LeaderEpoch& epoch)
    : PostTabletCreateTaskBase(
          catalog_manager, async_task_pool, messenger, std::move(table_info), std::move(epoch)) {}

std::string MarkTableAsRunningTask::description() const {
  return Format("MarkTableAsRunningTask [$0]", table_info_->id());
}

Status MarkTableAsRunningTask::FirstStep() {
  Complete();
  return Status::OK();
}

}  // namespace yb::master
