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

#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"

#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"

#include "yb/master/catalog_entity_info.pb.h"

#include "yb/util/memory/memory_usage.h"

namespace yb {
namespace client {

struct YBTableInfo {
  YBTableName table_name;
  std::string table_id;
  YBSchema schema;
  dockv::PartitionSchema partition_schema;
  qlexpr::IndexMap index_map;
  boost::optional<qlexpr::IndexInfo> index_info;
  YBTableType table_type;
  bool colocated; // Accounts for databases and tablegroups but not for YSQL system tables.
  boost::optional<master::ReplicationInfoPB> replication_info;
  boost::optional<uint32> wal_retention_secs;
  // Explicitly stores the PG table id (incase the table was rewritten).
  TableId pg_table_id;

  // Populated and used by GetTableSchema() for YSQL tables.
  boost::optional<google::protobuf::RepeatedPtrField<yb::master::YsqlDdlTxnVerifierStatePB>>
      ysql_ddl_txn_verifier_state;

  size_t DynamicMemoryUsage() const {
    size_t size =
        sizeof(*this) + DynamicMemoryUsageOf(table_name, table_id, schema, partition_schema);
    if (index_info) {
      size += index_info->DynamicMemoryUsage();
    }
    size += index_map.DynamicMemoryUsage();
    if (replication_info) {
      size += replication_info->SpaceUsedLong();
    }
    return size;
  }
};

Result<YBTableType> PBToClientTableType(TableType table_type_from_pb);
TableType ClientToPBTableType(YBTableType table_type);

struct TableSizeInfo {
  int64 table_size;
  int32 num_missing_tablets;
};

}  // namespace client
}  // namespace yb
