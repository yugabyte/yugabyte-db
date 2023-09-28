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

#include <chrono>

#include "yb/client/yb_table_name.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/snapshot.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.pb.h"

#include "yb/util/tsan_util.h"
#include "yb/util/status.h"


namespace yb {

/* Helper class for tests to use admin functionality. */
class TestAdminClient {
 public:
  TestAdminClient(ExternalMiniCluster* cluster, client::YBClient* ybclient)
      : cluster_(cluster), ybclient_(ybclient) {}

  Status SplitTablet(const TabletId& tablet_id);

  // If `tablet_id` is not provided a tablet backing `table` is chosen arbitrarily.
  Status SplitTablet(const client::YBTableName& table, const std::optional<TabletId>& tablet_id);
  Status SplitTablet(
      const std::string& ns, const std::string& table,
      const std::optional<TabletId>& tablet_id = std::nullopt);

  // If `tablet_id` is not provided a tablet backing `table` is chosen arbitrarily.
  Status SplitTabletAndWait(
      const client::YBTableName& table, bool wait_for_parent_deletion,
      const std::optional<TabletId>& tablet_id = std::nullopt);
  Status SplitTabletAndWait(
      const std::string& ns, const std::string& table, bool wait_for_parent_deletion,
      const std::optional<TabletId>& tablet_id = std::nullopt);

  Result<client::YBTableName> GetTableName(const std::string& ns, const std::string& table);

  Result<bool> IsTabletSplittingComplete(bool wait_for_parent_deletion);

  Result<std::vector<master::TabletLocationsPB>> GetTabletLocations(
      const client::YBTableName& table);
  Result<std::vector<master::TabletLocationsPB>> GetTabletLocations(
      const std::string& ns, const std::string& table);

  Status WaitForTabletPostSplitCompacted(size_t tserver_idx, const TabletId& tablet_id);

  Status FlushTable(const std::string& ns, const std::string& table);

  Result<TxnSnapshotId> CreateSnapshotAndWait(
      const SnapshotScheduleId& schedule_id = SnapshotScheduleId(Uuid::Nil()),
      const master::TableIdentifierPB& tables = {},
      const std::optional<int32_t> retention_duration_hours = std::nullopt);

  Status DeleteSnapshotAndWait(const TxnSnapshotId& snapshot_id);

  Result<TxnSnapshotId> CreateSnapshot(
      const SnapshotScheduleId& schedule_id, const master::TableIdentifierPB& tables,
      const std::optional<int32_t> retention_duration_hours);
  Status WaitForSnapshotComplete(const TxnSnapshotId& snapshot_id, bool check_deleted = false);

 private:
  ExternalMiniCluster* cluster_;
  client::YBClient* ybclient_;
};

}  // namespace yb
