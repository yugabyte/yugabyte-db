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

#ifndef YB_CLIENT_TABLE_H
#define YB_CLIENT_TABLE_H

#include "yb/client/yb_table_name.h"
#include "yb/client/schema.h"

#include "yb/common/common_fwd.h"
#include "yb/common/index.h"
#include "yb/common/partition.h"

DECLARE_int32(max_num_tablets_for_table);

namespace yb {
namespace client {

// This must match TableType in common.proto.
// We have static_assert's in tablet-test.cc to verify this.
enum YBTableType {
  YQL_TABLE_TYPE = 2,
  REDIS_TABLE_TYPE = 3,
  PGSQL_TABLE_TYPE = 4,
  TRANSACTION_STATUS_TABLE_TYPE = 5,
  UNKNOWN_TABLE_TYPE = -1
};

struct YBTableInfo {
  YBTableName table_name;
  std::string table_id;
  YBSchema schema;
  PartitionSchema partition_schema;
  IndexMap index_map;
  boost::optional<IndexInfo> index_info;
  YBTableType table_type;
};

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
  ~YBTable();

  static Status PBToClientTableType(TableType table_type_from_pb, YBTableType* client_table_type);
  static TableType ClientToPBTableType(YBTableType table_type);

  //------------------------------------------------------------------------------------------------
  // Access functions.

  const YBTableName& name() const;

  YBTableType table_type() const;

  // Return the table's ID. This is an internal identifier which uniquely
  // identifies a table. If the table is deleted and recreated with the same
  // name, the ID will distinguish the old table from the new.
  const std::string& id() const;

  YBClient* client() const;
  const YBSchema& schema() const;
  const Schema& InternalSchema() const;
  const PartitionSchema& partition_schema() const;

  const std::vector<std::string>& GetPartitions() const;

  // Indexes available on the table.
  const IndexMap& index_map() const;

  // Is this an index?
  bool IsIndex() const;

  // For index table: information about this index.
  const IndexInfo& index_info() const;

  //------------------------------------------------------------------------------------------------
  // CQL support
  // Create a new QL operation for this table.
  YBqlWriteOp* NewQLWrite();
  YBqlWriteOp* NewQLInsert();
  YBqlWriteOp* NewQLUpdate();
  YBqlWriteOp* NewQLDelete();

  YBqlReadOp* NewQLRead();
  YBqlReadOp* NewQLSelect();

  // Finds partition start for specified partition_key.
  // Partitions could be groupped by group_by bunches, in this case start of such bunch is returned.
  const std::string& FindPartitionStart(
      const std::string& partition_key, size_t group_by = 1) const;

  //------------------------------------------------------------------------------------------------
  // Postgres support
  // Create a new QL operation for this table.
  YBPgsqlWriteOp* NewPgsqlWrite();
  YBPgsqlWriteOp* NewPgsqlInsert();
  YBPgsqlWriteOp* NewPgsqlUpdate();
  YBPgsqlWriteOp* NewPgsqlDelete();

  YBPgsqlReadOp* NewPgsqlRead();
  YBPgsqlReadOp* NewPgsqlSelect();

 private:
  friend class YBClient;
  friend class internal::GetTableSchemaRpc;

  YBTable(client::YBClient* client, const YBTableInfo& info);

  CHECKED_STATUS Open();

  client::YBClient* const client_;
  YBTableType table_type_;
  YBTableInfo info_;
  std::vector<std::string> partitions_;

  DISALLOW_COPY_AND_ASSIGN(YBTable);
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TABLE_H
