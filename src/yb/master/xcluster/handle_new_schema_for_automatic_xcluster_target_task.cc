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

#include "yb/master/xcluster/handle_new_schema_for_automatic_xcluster_target_task.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_error.h"
#include "yb/master/master_replication.pb.h"
#include "yb/util/status.h"

#define SCHEDULE_WITH_DELAY(task, ...) \
  ScheduleNextStepWithDelay( \
      std::bind(&HandleNewSchemaForAutomaticXClusterTargetTask::task, this, ##__VA_ARGS__), #task, \
      kScheduleDelay);

#define SCHEDULE(task, ...) \
  ScheduleNextStep( \
      std::bind(&HandleNewSchemaForAutomaticXClusterTargetTask::task, this, ##__VA_ARGS__), \
      #task);

namespace yb::master {

namespace {
const MonoDelta kScheduleDelay = MonoDelta::FromMilliseconds(200);
}

HandleNewSchemaForAutomaticXClusterTargetTask::HandleNewSchemaForAutomaticXClusterTargetTask(
    CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
    TableInfoPtr table_info, const LeaderEpoch& epoch, Master& master,
    const xcluster::ReplicationGroupId& replication_group_id, const xrepl::StreamId& stream_id,
    uint32_t producer_schema_version, const SchemaPB& schema,
    std::function<Status(const TableId&, const SchemaPB&, uint32_t)>
        create_async_insert_packed_schema_tasks_fn)
    : MultiStepTableTaskBase(catalog_manager, async_task_pool, messenger, table_info, epoch),
      replication_group_id_(replication_group_id),
      stream_id_(stream_id),
      producer_schema_version_(producer_schema_version),
      schema_(schema),
      master_(master),
      create_async_insert_packed_schema_tasks_fn_(
          std::move(create_async_insert_packed_schema_tasks_fn)) {}

std::string HandleNewSchemaForAutomaticXClusterTargetTask::description() const {
  return Format(
      "HandleNewSchemaForAutomaticXClusterTarget[$0:$1:$2]", replication_group_id_, stream_id_,
      table_info_->id());
}

Status HandleNewSchemaForAutomaticXClusterTargetTask::FirstStep() {
  // Step 1: Query all tablets for their latest compatible schema version.
  auto tablets = VERIFY_RESULT(table_info_->GetTablets());
  SCHECK(!tablets.empty(), IllegalState, "No tablets found for table $0", table_info_->id());
  {
    std::lock_guard l(mutex_);
    tablet_responses_.clear();
    tablet_responses_.resize(tablets.size());
    pending_responses_count_ = tablets.size();
  }

  // TODO(jhe): If there's only one tablet (e.g. colocated) we can probably skip some steps.
  for (size_t i = 0; i < tablets.size(); ++i) {
    auto call = std::make_shared<AsyncGetLatestCompatibleSchemaVersion>(
        &master_, catalog_manager_.AsyncTaskPool(), tablets[i], table_info_, schema_, epoch_,
        [retained_self = shared_from_this(), i, tablet_id = tablets[i]->id()](
            const Status& status, uint32_t schema_version) {
          auto self = std::static_pointer_cast<HandleNewSchemaForAutomaticXClusterTargetTask>(
              retained_self);
          std::lock_guard l(self->mutex_);
          self->tablet_responses_[i] = {status, schema_version, tablet_id};
          --self->pending_responses_count_;
          if (self->pending_responses_count_ == 0) {
            self->ScheduleNextStep(
                std::bind(&HandleNewSchemaForAutomaticXClusterTargetTask::CheckResponses, self),
                "CheckResponses");
          }
        });
    table_info_->AddTask(call);
    RETURN_NOT_OK(catalog_manager_.ScheduleTask(call));
  }

  return Status::OK();
}

Status HandleNewSchemaForAutomaticXClusterTargetTask::CheckResponses() {
  // Step 2: Check if all tablets have the same latest compatible schema version.
  std::lock_guard l(mutex_);
  DCHECK_EQ(pending_responses_count_, 0);

  std::optional<uint32_t> common_schema_version;
  TabletId sample_tablet_id;
  bool all_succeeded = true;
  for (const auto& [status, latest_compatible_schema_version, tablet_id] : tablet_responses_) {
    if (!status.ok()) {
      VLOG_WITH_PREFIX(1) << "Failed to get compatible schema version for tablet " << tablet_id
                          << ": " << status;
      all_succeeded = false;
      break;
    }
    if (!common_schema_version.has_value()) {
      common_schema_version = latest_compatible_schema_version;
      sample_tablet_id = tablet_id;
    } else if (*common_schema_version != latest_compatible_schema_version) {
      VLOG_WITH_PREFIX(1) << "Found different compatible schema versions. Tablet "
                          << sample_tablet_id << " has schema version " << *common_schema_version
                          << ", but tablet " << tablet_id << " has schema version "
                          << latest_compatible_schema_version;
      all_succeeded = false;
      break;
    }
  }

  if (all_succeeded) {
    // All tablets have the same latest compatible schema version, update mapping.
    return UpdateSchemaVersionMapping(*common_schema_version);
  }

  // If we don't find a common schema version, then we need to insert a new schema.
  return InsertPackedSchema();
}

Status HandleNewSchemaForAutomaticXClusterTargetTask::InsertPackedSchema() {
  // Step 3: Insert new packed schema.
  uint32_t current_schema_version = table_info_->LockForRead()->pb.version();
  RETURN_NOT_OK(create_async_insert_packed_schema_tasks_fn_(
      table_info_->id(), schema_, current_schema_version));

  // At this point we've inserted a new schema at version [current_schema_version + 1].
  // TODO(#29761): Further retries should use this schema version instead of repeatedly inserting
  // the same schema again.

  // Wait for insert packed schema tasks to complete before retrying.
  SCHEDULE_WITH_DELAY(WaitForInsertPackedSchemaTasks);

  return Status::OK();
}

Status HandleNewSchemaForAutomaticXClusterTargetTask::WaitForInsertPackedSchemaTasks() {
  // Step 4: Wait for insert packed schema tasks to complete before retrying.
  YB_LOG_EVERY_N_SECS(INFO, 30) << LogPrefix()
                                << "Waiting for insert packed schema tasks to complete for table "
                                << table_info_->ToString();
  bool has_insert_packed_schema_tasks =
      table_info_->HasTasks(server::MonitoredTaskType::kInsertPackedSchemaForXClusterTarget);

  if (has_insert_packed_schema_tasks) {
    // Continue waiting for insert packed schema tasks to complete.
    SCHEDULE_WITH_DELAY(WaitForInsertPackedSchemaTasks);
    return Status::OK();
  }

  // All insert packed schema tasks have completed. Retry querying tablets for compatible schema.
  SCHEDULE(FirstStep);
  return Status::OK();
}

Status HandleNewSchemaForAutomaticXClusterTargetTask::UpdateSchemaVersionMapping(
    uint32_t compatible_schema_version) {
  // Final step: Update the producer->consumer schema versions mapping and return.
  //
  // To ensure we don't run into issues with missing schema versions, we maintain the invariant here
  // that this mapping is ONLY updated once we know for sure that all tablets have this same
  // compatible schema version.
  // Pollers are blocked until this mapping gets updated, so will be able to continue replicating
  // after this.
  UpdateConsumerOnProducerMetadataResponsePB resp;
  uint32_t colocation_id = kColocationIdNotSet;
  if (schema_.has_colocated_table_id() && schema_.colocated_table_id().has_colocation_id()) {
    colocation_id = schema_.colocated_table_id().colocation_id();
  }
  // Set check_min_consumer_schema_version = true to verify that the compatible schema version is >=
  // to the minimum consumer schema version in the mapping (to ensure it hasn't been GC-ed).
  auto s = catalog_manager_.DoUpdateConsumerOnProducerMetadata(
      replication_group_id_, stream_id_, producer_schema_version_, compatible_schema_version,
      colocation_id, /*check_min_consumer_schema_version=*/true, &resp);
  if (!s.ok()) {
    if (MasterError(s) == MasterErrorPB::XCLUSTER_CONSUMER_SCHEMA_VERSION_TOO_OLD) {
      // Consumer schema version is older than the minimum consumer schema version in the mapping.
      // Since we use the min consumer schema version for preventing schema GC, it is possible that
      // this consumer schema version could get GC-ed as we complete this task.
      // Therefore we can't use this old schema version, and need to reinsert this schema.
      LOG_WITH_PREFIX(INFO) << "Consumer schema version is older than the minimum consumer "
                            << "schema version in the mapping. Reinserting schema. " << s;
      return InsertPackedSchema();
    }
    return s;
  }
  Complete();
  return Status::OK();
}

}  // namespace yb::master
