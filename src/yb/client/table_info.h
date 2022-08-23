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

#ifndef YB_CLIENT_TABLE_INFO_H
#define YB_CLIENT_TABLE_INFO_H

#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/index.h"
#include "yb/common/partition.h"

#include "yb/master/catalog_entity_info.pb.h"

namespace yb {
namespace client {

struct YBTableInfo {
  YBTableName table_name;
  std::string table_id;
  YBSchema schema;
  PartitionSchema partition_schema;
  IndexMap index_map;
  boost::optional<IndexInfo> index_info;
  YBTableType table_type;
  bool colocated; // Accounts for databases and tablegroups but not for YSQL system tables.
  boost::optional<master::ReplicationInfoPB> replication_info;
  boost::optional<uint32> wal_retention_secs;
};

Result<YBTableType> PBToClientTableType(TableType table_type_from_pb);
TableType ClientToPBTableType(YBTableType table_type);

struct TableSizeInfo {
  int64 table_size;
  int32 num_missing_tablets;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_TABLE_INFO_H
