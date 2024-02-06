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
  outbound_replication_group_->AddTable(
      table_info_, epoch_, std::bind(&AddTableToXClusterSourceTask::CompletionCallback, this, _1));

  return Status::OK();
}

void AddTableToXClusterSourceTask::CompletionCallback(const Status& status) {
  if (status.ok()) {
    Complete();
    return;
  }

  AbortAndReturnPrevState(status);
}

}  // namespace yb::master
