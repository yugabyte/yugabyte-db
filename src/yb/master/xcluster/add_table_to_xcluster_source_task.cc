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

#include "yb/master/xcluster/add_table_to_xcluster_source_task.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group.h"

namespace yb::master {

using namespace std::placeholders;

AddTableToXClusterSourceTask::AddTableToXClusterSourceTask(
    std::shared_ptr<XClusterOutboundReplicationGroup> outbound_replication_group,
    CatalogManager& catalog_manager, rpc::Messenger& messenger, TableInfoPtr table_info,
    const LeaderEpoch& epoch)
    : PostTabletCreateTaskBase(
          catalog_manager, *catalog_manager.AsyncTaskPool(), messenger, std::move(table_info),
          epoch),
      outbound_replication_group_(std::move(outbound_replication_group)) {}

std::string AddTableToXClusterSourceTask::description() const {
  return Format("AddTableToXClusterSourceTask [$0]", table_info_->id());
}

Status AddTableToXClusterSourceTask::FirstStep() {
  RETURN_NOT_OK(outbound_replication_group_->CreateStreamForNewTable(
      table_info_->namespace_id(), table_info_->id(), epoch_));

  ScheduleNextStep(
      std::bind(&AddTableToXClusterSourceTask::CheckpointStream, this), "CheckpointStream");
  return Status::OK();
}

Status AddTableToXClusterSourceTask::CheckpointStream() {
  RETURN_NOT_OK(outbound_replication_group_->CheckpointNewTable(
      table_info_->namespace_id(), table_info_->id(), epoch_,
      std::bind(&AddTableToXClusterSourceTask::CheckpointCompletionCallback, this, _1)));

  return Status::OK();
}

void AddTableToXClusterSourceTask::CheckpointCompletionCallback(const Status& status) {
  if (!status.ok()) {
    AbortAndReturnPrevState(status);
    return;
  }

  ScheduleNextStep(
      std::bind(&AddTableToXClusterSourceTask::MarkTableAsCheckpointed, this),
      "MarkTableAsCheckpointed");
}

Status AddTableToXClusterSourceTask::MarkTableAsCheckpointed() {
  RETURN_NOT_OK(outbound_replication_group_->MarkNewTablesAsCheckpointed(
      table_info_->namespace_id(), table_info_->id(), epoch_));

  const auto stream_id = VERIFY_RESULT(
      outbound_replication_group_->GetStreamId(table_info_->namespace_id(), table_info_->id()));
  LOG_WITH_PREFIX(INFO)
      << "Table " << table_info_->ToString()
      << " successfully checkpointed for xCluster universe replication with Stream: " << stream_id;

  Complete();
  return Status::OK();
}

}  // namespace yb::master
