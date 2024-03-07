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

#include "yb/master/xcluster/xcluster_outbound_replication_group_tasks.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group.h"

using namespace std::placeholders;

namespace yb::master {

XClusterOutboundReplicationGroupTaskFactory::XClusterOutboundReplicationGroupTaskFactory(
    std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func,
    ThreadPool& async_task_pool, rpc::Messenger& messenger)
    : validate_epoch_func_(std::move(validate_epoch_func)),
      async_task_pool_(async_task_pool),
      messenger_(messenger) {}

std::shared_ptr<XClusterCheckpointNamespaceTask>
XClusterOutboundReplicationGroupTaskFactory::CreateCheckpointNamespaceTask(
    XClusterOutboundReplicationGroup& outbound_replication_group, const NamespaceId& namespace_id,
    const LeaderEpoch& epoch) {
  return std::make_shared<XClusterCheckpointNamespaceTask>(
      outbound_replication_group, namespace_id, validate_epoch_func_, async_task_pool_, messenger_,
      epoch);
}

XClusterCheckpointNamespaceTask::XClusterCheckpointNamespaceTask(
    XClusterOutboundReplicationGroup& outbound_replication_group, const NamespaceId& namespace_id,
    std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func,
    ThreadPool& async_task_pool, rpc::Messenger& messenger, const LeaderEpoch& epoch)
    : MultiStepCatalogEntityTask(
          std::move(validate_epoch_func), async_task_pool, messenger, outbound_replication_group,
          epoch),
      namespace_id_(namespace_id),
      outbound_replication_group_(outbound_replication_group) {}

std::string XClusterCheckpointNamespaceTask::description() const {
  return Format("XClusterCheckpointNamespaceTask [$0]", outbound_replication_group_.Id());
}

Status XClusterCheckpointNamespaceTask::FirstStep() {
  RETURN_NOT_OK(
      outbound_replication_group_.CreateStreamsForInitialBootstrap(namespace_id_, epoch_));
  ScheduleNextStep(
      std::bind(&XClusterCheckpointNamespaceTask::CheckpointStreams, this), "CheckpointStreams");
  return Status::OK();
}

Status XClusterCheckpointNamespaceTask::CheckpointStreams() {
  RETURN_NOT_OK(outbound_replication_group_.CheckpointStreamsForInitialBootstrap(
      namespace_id_, epoch_,
      std::bind(&XClusterCheckpointNamespaceTask::CheckpointStreamsCallback, this, _1)));

  return Status::OK();
}

void XClusterCheckpointNamespaceTask::CheckpointStreamsCallback(
    XClusterCheckpointStreamsResult result) {
  ScheduleNextStep(
      std::bind(
          &XClusterCheckpointNamespaceTask::MarkTablesAsCheckpointed, this, std::move(result)),
      "MarkTablesAsCheckpointed");
}

Status XClusterCheckpointNamespaceTask::MarkTablesAsCheckpointed(
    XClusterCheckpointStreamsResult result) {
  if (VERIFY_RESULT(outbound_replication_group_.MarkBootstrapTablesAsCheckpointed(
          namespace_id_, std::move(result), epoch_))) {
    // All tables have been checkpointed and the replication group is now READY.
    Complete();
  } else {
    ScheduleNextStep(
        std::bind(&XClusterCheckpointNamespaceTask::CheckpointStreams, this), "CheckpointStreams");
  }
  return Status::OK();
}

void XClusterCheckpointNamespaceTask::TaskCompleted(const Status& status) {
  if (!status.ok()) {
    outbound_replication_group_.MarkCheckpointNamespaceAsFailed(namespace_id_, epoch_, status);
  }
}

} // namespace yb::master
