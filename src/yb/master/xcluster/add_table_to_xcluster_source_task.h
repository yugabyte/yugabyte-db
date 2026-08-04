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

#include "yb/client/client_fwd.h"

#include "yb/master/post_tablet_create_task_base.h"

namespace yb::master {

class XClusterOutboundReplicationGroup;

class AddTableToXClusterSourceTask : public PostTabletCreateTaskBase {
 public:
  explicit AddTableToXClusterSourceTask(
      std::shared_ptr<XClusterOutboundReplicationGroup> outbound_replication_group,
      CatalogManager& catalog_manager, rpc::Messenger& messenger, TableInfoPtr table_info,
      const LeaderEpoch& epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToXClusterSource;
  }

  std::string type_name() const override { return "Add table to xCluster source replication"; }

  std::string description() const override;

 private:
  Status FirstStep() override;

  Status CheckpointStream();

  void CheckpointCompletionCallback(const Status& status);

  Status MarkTableAsCheckpointed();

  const std::shared_ptr<XClusterOutboundReplicationGroup> outbound_replication_group_;
};

// Same as AddTableToXClusterSourceTask but does not require an outbound replication group.
// This is used for bi-directional xCluster indexes only.
class CreateXClusterStreamForBiDirectionalIndexTask : public PostTabletCreateTaskBase {
 public:
  explicit CreateXClusterStreamForBiDirectionalIndexTask(
      std::function<Result<xrepl::StreamId>(const TableId&, const LeaderEpoch&, StdStatusCallback)>
          checkpoint_table_func,
      CatalogManager& catalog_manager, rpc::Messenger& messenger, TableInfoPtr table_info,
      const LeaderEpoch& epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToXClusterSource;
  }

  std::string type_name() const override { return "Create xCluster stream for table"; }

  std::string description() const override;

 private:
  Status FirstStep() override;

  void CheckpointCompletionCallback(const Status& status);

  std::function<Result<xrepl::StreamId>(
      const TableId&, const LeaderEpoch& epoch, StdStatusCallback callback)>
      checkpoint_table_func_;
};

}  // namespace yb::master
