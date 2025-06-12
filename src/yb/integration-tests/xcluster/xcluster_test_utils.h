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

#include "yb/common/entity_ids_types.h"
#include "yb/cdc/xcluster_types.h"

namespace yb {

namespace client {
class YBClient;
}  // namespace client

namespace XClusterTestUtils {

template <typename Cluster>
Status RunOnBothClusters(
    Cluster* producer_cluster, Cluster* consumer_cluster,
    const std::function<Status(Cluster*)>& run_on_cluster) {
  auto producer_future =
      std::async(std::launch::async, [&] { return run_on_cluster(producer_cluster); });
  auto consumer_future =
      std::async(std::launch::async, [&] { return run_on_cluster(consumer_cluster); });

  auto producer_status = producer_future.get();
  auto consumer_status = consumer_future.get();

  RETURN_NOT_OK_PREPEND(producer_status, "Producer cluster operation failed");
  RETURN_NOT_OK_PREPEND(consumer_status, "Consumer cluster operation failed");
  return Status::OK();
}

Result<NamespaceId> GetNamespaceId(
    client::YBClient& client, const NamespaceName& ns_name);

Result<bool> CheckpointReplicationGroup(
    client::YBClient& producer_client, const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceName& namespace_name, MonoDelta timeout, bool automatic_ddl_mode = false);

Status CreateReplicationFromCheckpoint(
    client::YBClient& producer_client, const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addr, MonoDelta timeout);

}  // namespace XClusterTestUtils
}  // namespace yb
