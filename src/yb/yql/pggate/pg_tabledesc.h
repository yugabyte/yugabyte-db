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

#ifndef YB_YQL_PGGATE_PG_TABLEDESC_H_
#define YB_YQL_PGGATE_PG_TABLEDESC_H_

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/client/yb_op.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------

// This class can be used to describe any reference of a column.
class PgTableDesc : public RefCountedThreadSafe<PgTableDesc> {
 public:
  explicit PgTableDesc(const client::YBTablePtr& table);

  const client::YBTableName& table_name() const;

  const PartitionSchema& partition_schema() const;

  size_t num_hash_key_columns() const;
  size_t num_key_columns() const;
  size_t num_columns() const;

  // Find the column given the postgres attr number.
  Result<size_t> FindColumn(int attr_num) const;

  Result<YBCPgColumnInfo> GetColumnInfo(int16_t attr_number) const;

  bool IsHashPartitioned() const;

  bool IsRangePartitioned() const;

  const std::vector<std::string>& GetPartitions() const;

  int GetPartitionCount() const;

  // When reading a row given its associated ybctid, the ybctid value is decoded to the row.
  Result<string> DecodeYbctid(const Slice& ybctid) const;

  // Seek the tablet partition where the row whose "ybctid" value was given can be found.
  Result<size_t> FindPartitionIndex(const Slice& ybctid) const;

  // These values are set by  PgGate to optimize query to narrow the scanning range of a query.
  CHECKED_STATUS SetScanBoundary(PgsqlReadRequestPB *req,
                                 const string& partition_lower_bound,
                                 bool lower_bound_is_inclusive,
                                 const string& partition_upper_bound,
                                 bool upper_bound_is_inclusive);

  const Schema& schema() const;

  bool IsColocated() const;

  uint32_t schema_version() const;

  // TODO(PgClient) Remove those operations.
  std::unique_ptr<client::YBPgsqlWriteOp> NewPgsqlInsert();
  std::unique_ptr<client::YBPgsqlWriteOp> NewPgsqlUpdate();
  std::unique_ptr<client::YBPgsqlWriteOp> NewPgsqlDelete();
  std::unique_ptr<client::YBPgsqlWriteOp> NewPgsqlTruncateColocated();

  std::unique_ptr<client::YBPgsqlReadOp> NewPgsqlSelect();
  std::unique_ptr<client::YBPgsqlReadOp> NewPgsqlSample();

 private:
  // TODO(PgClient) While we have YbOps on postgres side, we have to keep YBTable.
  client::YBTablePtr table_;

  const std::shared_ptr<const client::VersionedTablePartitionList> table_partitions_;

  // Attr number to column index map.
  std::unordered_map<int, size_t> attr_num_map_;
};

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_TABLEDESC_H_
