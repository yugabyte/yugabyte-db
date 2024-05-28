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

#include "yb/common/entity_ids_types.h"
#include "yb/master/multi_step_monitored_task.h"
#include "yb/master/xcluster/master_xcluster_types.h"

namespace yb {

namespace master {

class XClusterOutboundReplicationGroup;

class XClusterCheckpointNamespaceTask : public MultiStepCatalogEntityTask {
 public:
  XClusterCheckpointNamespaceTask(
      XClusterOutboundReplicationGroup& outbound_replication_group, const NamespaceId& namespace_id,
      std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func,
      ThreadPool& async_task_pool, rpc::Messenger& messenger, const LeaderEpoch& epoch);

  ~XClusterCheckpointNamespaceTask() = default;

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddNamespaceToXClusterSource;
  }

  std::string type_name() const override {
    return "Add namespace to xCluster outbound replication group";
  }

  std::string description() const override;

  Status FirstStep() override;

 private:
  Status CheckpointStreams();
  void CheckpointStreamsCallback(XClusterCheckpointStreamsResult result);
  Status MarkTablesAsCheckpointed(XClusterCheckpointStreamsResult result);
  void TaskCompleted(const Status& s) override;
  MonoDelta GetDelayWithBackoff();

  const NamespaceId namespace_id_;
  XClusterOutboundReplicationGroup& outbound_replication_group_;
  MonoDelta delay_with_backoff_ = MonoDelta::FromMilliseconds(100);
};

class XClusterOutboundReplicationGroupTaskFactory {
 public:
  XClusterOutboundReplicationGroupTaskFactory(
      std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func,
      ThreadPool& async_task_pool, rpc::Messenger& messenger);

  virtual ~XClusterOutboundReplicationGroupTaskFactory() = default;

  std::shared_ptr<XClusterCheckpointNamespaceTask> CreateCheckpointNamespaceTask(
      XClusterOutboundReplicationGroup& outbound_replication_group, const NamespaceId& namespace_id,
      const LeaderEpoch& epoch);

 protected:
  std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func_;
  ThreadPool& async_task_pool_;
  rpc::Messenger& messenger_;
};

}  // namespace master

}  // namespace yb
