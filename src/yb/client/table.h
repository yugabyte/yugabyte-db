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

#include "yb/util/flags.h"

#include "yb/client/client_fwd.h"

#include "yb/common/common_fwd.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/master/master_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/locks.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_fwd.h"

DECLARE_int32(max_num_tablets_for_table);

namespace yb {
namespace client {

// This must match TableType in common.proto.
// We have static_assert's in tablet-test.cc to verify this.
enum class YBTableType {
  YQL_TABLE_TYPE = 2,
  REDIS_TABLE_TYPE = 3,
  PGSQL_TABLE_TYPE = 4,
  TRANSACTION_STATUS_TABLE_TYPE = 5,
  UNKNOWN_TABLE_TYPE = -1
};

struct VersionedTablePartitionList {
  TablePartitionList keys;
  // See SysTablesEntryPB::partition_list_version.
  PartitionListVersion version;

  std::string ToString() const;
};

typedef Result<VersionedTablePartitionListPtr> FetchPartitionsResult;
typedef std::function<void(const FetchPartitionsResult&)> FetchPartitionsCallback;

// A YBTable represents a table on a particular cluster. It holds the current
// schema of the table. Any given YBTable instance belongs to a specific YBClient
// instance.
//
// Upon construction, the table is looked up in the catalog (or catalog cache),
// and the schema fetched for introspection.
//
// This class is thread-safe.
class YBTable : public std::enable_shared_from_this<YBTable> {
 public:
  YBTable(const YBTableInfo& info, VersionedTablePartitionListPtr partitions);

  ~YBTable();

  // Fetches tablet partitions from master using GetTableLocations RPC.
  static void FetchPartitions(
      YBClient* client, const TableId& table_id, FetchPartitionsCallback callback);

  //------------------------------------------------------------------------------------------------
  // Access functions.

  const YBTableName& name() const;

  YBTableType table_type() const;

  // Return the table's ID. This is an internal identifier which uniquely
  // identifies a table. If the table is deleted and recreated with the same
  // name, the ID will distinguish the old table from the new.
  const std::string& id() const;

  const YBSchema& schema() const;
  const Schema& InternalSchema() const;
  const dockv::PartitionSchema& partition_schema() const;
  bool IsHashPartitioned() const;
  bool IsRangePartitioned() const;

  // Note that table partitions are mutable could change at any time because of tablet splitting.
  // So it is not safe to rely on following Get*Partition* functions to return information that
  // is consistent across subsequent calls.
  std::shared_ptr<const TablePartitionList> GetPartitionsShared() const;
  VersionedTablePartitionListPtr GetVersionedPartitions() const;
  TablePartitionList GetPartitionsCopy() const;
  int32_t GetPartitionCount() const;
  PartitionListVersion GetPartitionListVersion() const;

  // Indexes available on the table.
  const qlexpr::IndexMap& index_map() const;

  // Is this an index?
  bool IsIndex() const;

  bool IsUniqueIndex() const;

  // For index table: information about this index.
  const qlexpr::IndexInfo& index_info() const;

  // True if the table is colocated (including tablegroups, excluding YSQL system tables).
  bool colocated() const;

  // Returns the replication info for the table.
  const boost::optional<master::ReplicationInfoPB>& replication_info() const;

  std::string ToString() const;
  //------------------------------------------------------------------------------------------------
  // CQL support
  // Create a new QL operation for this table.
  std::unique_ptr<YBqlWriteOp> NewQLWrite();
  std::unique_ptr<YBqlWriteOp> NewQLInsert();
  std::unique_ptr<YBqlWriteOp> NewQLUpdate();
  std::unique_ptr<YBqlWriteOp> NewQLDelete();

  std::unique_ptr<YBqlReadOp> NewQLRead();
  std::unique_ptr<YBqlReadOp> NewQLSelect();

  // Finds partition start for specified partition_key.
  // Partitions could be grouped by group_by bunches, in this case start of such bunch is returned.
  PartitionKeyPtr FindPartitionStart(const PartitionKey& partition_key, size_t group_by = 1) const;

  void MarkPartitionsAsStale();
  bool ArePartitionsStale() const;

  // Asynchronously refreshes table partitions.
  void RefreshPartitions(YBClient* client, StdStatusCallback callback);

  size_t DynamicMemoryUsage() const;

 private:
  friend class YBClient;
  friend class internal::GetTableSchemaRpc;
  friend class internal::GetTablegroupSchemaRpc;
  friend class internal::GetColocatedTabletSchemaRpc;

  void InvokeRefreshPartitionsCallbacks(const Status& status);

  size_t FindPartitionStartIndex(const std::string& partition_key, size_t group_by = 1) const;

  const std::unique_ptr<const YBTableInfo> info_;

  // Mutex protecting partitions_.
  mutable rw_spinlock mutex_;
  VersionedTablePartitionListPtr partitions_ GUARDED_BY(mutex_);

  std::atomic<bool> partitions_are_stale_{false};

  std::mutex refresh_partitions_callbacks_mutex_;
  std::vector<StdStatusCallback> refresh_partitions_callbacks_
      GUARDED_BY(refresh_partitions_callbacks_mutex_);

  DISALLOW_COPY_AND_ASSIGN(YBTable);
};

size_t FindPartitionStartIndex(
    const TablePartitionList& partitions, std::string_view partition_key, size_t group_by = 1);

PartitionKeyPtr FindPartitionStart(
    const VersionedTablePartitionListPtr& versioned_partitions, const PartitionKey& partition_key,
    size_t group_by = 1);

} // namespace client
} // namespace yb
