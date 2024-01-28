//--------------------------------------------------------------------------------------------------
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
//
// Structure definitions for a Postgres table descriptor.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/pg_types.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/dockv/partition.h"
#include "yb/dockv/schema_packing.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
class PgClient;

// This class can be used to describe any reference of a column.
class PgTableDesc : public RefCountedThreadSafe<PgTableDesc> {
 public:
  PgTableDesc(const PgObjectId& relfilenode_id, const master::GetTableSchemaResponsePB& resp,
              client::VersionedTablePartitionList partition_list);

  Status Init();

  const PgObjectId& relfilenode_id() const {
    return relfilenode_id_;
  }

  PgOid pg_table_id() const {
    return pg_table_id_ != kInvalidOid ? pg_table_id_ : relfilenode_id_.object_oid;
  }

  const client::YBTableName& table_name() const;

  const dockv::PartitionSchema& partition_schema() const;

  size_t num_range_key_columns() const;
  size_t num_hash_key_columns() const;
  size_t num_key_columns() const;
  size_t num_columns() const;

  // Find the column given the postgres attr number.
  Result<size_t> FindColumn(int attr_num) const;

  Result<YBCPgColumnInfo> GetColumnInfo(int attr_number) const;

  bool IsHashPartitioned() const;

  bool IsRangePartitioned() const;

  const client::TablePartitionList& GetPartitionList() const;

  size_t GetPartitionListSize() const;

  client::PartitionListVersion GetPartitionListVersion() const;

  void SetLatestKnownPartitionListVersion(client::PartitionListVersion version);

  Status EnsurePartitionListIsUpToDate(PgClient* client);

  // When reading a row given its associated ybctid, the ybctid value is decoded to the row.
  Result<std::string> DecodeYbctid(const Slice& ybctid) const;

  // Seek the tablet partition where the row whose "ybctid" value was given can be found.
  Result<size_t> FindPartitionIndex(const Slice& ybctid) const;

  // Check if boundaries set on request define valid (not empty) range
  static Result<bool> CheckScanBoundary(LWPgsqlReadRequestPB* req);
  // These values are set by  PgGate to optimize query to narrow the scanning range of a query.
  // Returns false if new boundary makes request range empty.
  static Result<bool> SetScanBoundary(LWPgsqlReadRequestPB* req,
                                      const std::string& partition_lower_bound,
                                      bool lower_bound_is_inclusive,
                                      const std::string& partition_upper_bound,
                                      bool upper_bound_is_inclusive);

  const Schema& schema() const;

  const dockv::SchemaPacking& schema_packing() const {
    return *schema_packing_;
  }

  // True if table is colocated (including tablegroups, excluding YSQL system tables).
  bool IsColocated() const;

  YBCPgOid GetColocationId() const;

  YBCPgOid GetTablegroupOid() const;

  uint32_t schema_version() const;

  bool IsIndex() const;

 private:
  PgObjectId relfilenode_id_;
  YBCPgOid pg_table_id_{kInvalidOid};
  master::GetTableSchemaResponsePB resp_;
  client::VersionedTablePartitionList table_partition_list_;
  client::PartitionListVersion latest_known_table_partition_list_version_;

  client::YBTableName table_name_;
  Schema schema_;
  dockv::PartitionSchema partition_schema_;
  std::optional<dockv::SchemaPacking> schema_packing_;

  // Attr number to column index map.
  std::vector<std::pair<int, size_t>> attr_num_map_;
  YBCPgOid tablegroup_oid_{kInvalidOid};
};

}  // namespace pggate
}  // namespace yb
