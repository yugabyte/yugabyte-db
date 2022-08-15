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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_tabledesc.h"

#include "yb/common/partition.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/doc_key.h"

#include "yb/gutil/casts.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace pggate {

PgTableDesc::PgTableDesc(
    const PgObjectId& id, const master::GetTableSchemaResponsePB& resp,
    std::shared_ptr<client::VersionedTablePartitionList> partitions)
    : id_(id), resp_(resp),  table_partitions_(std::move(partitions)) {
  table_name_.GetFromTableIdentifierPB(resp.identifier());
}

Status PgTableDesc::Init() {
  RETURN_NOT_OK(SchemaFromPB(resp_.schema(), &schema_));
  size_t idx = 0;
  for (const auto& column : schema().columns()) {
    attr_num_map_.emplace(column.order(), idx++);
  }
  if (resp_.has_tablegroup_id()) {
    tablegroup_oid_ = VERIFY_RESULT(GetPgsqlTablegroupOid(resp_.tablegroup_id()));
  }
  return PartitionSchema::FromPB(resp_.partition_schema(), schema_, &partition_schema_);
}

Result<size_t> PgTableDesc::FindColumn(int attr_num) const {
  // Find virtual columns.
  if (attr_num == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    return num_columns();
  }

  // Find physical column.
  const auto itr = attr_num_map_.find(attr_num);
  if (itr != attr_num_map_.end()) {
    return itr->second;
  }

  return STATUS_FORMAT(InvalidArgument, "Invalid column number $0", attr_num);
}

Result<YBCPgColumnInfo> PgTableDesc::GetColumnInfo(int16_t attr_number) const {
  YBCPgColumnInfo column_info {
    .is_primary = false,
    .is_hash = false
  };
  const auto itr = attr_num_map_.find(attr_number);
  if (itr != attr_num_map_.end()) {
    column_info.is_primary = itr->second < schema().num_key_columns();
    column_info.is_hash = itr->second < schema().num_hash_key_columns();
  }
  return column_info;
}

bool PgTableDesc::IsColocated() const {
  return resp_.colocated();
}

YBCPgOid PgTableDesc::GetColocationId() const {
  return schema().has_colocation_id() ? schema().colocation_id() : kColocationIdNotSet;
}

YBCPgOid PgTableDesc::GetTablegroupOid() const {
  return tablegroup_oid_;
}

bool PgTableDesc::IsHashPartitioned() const {
  return schema().num_hash_key_columns() > 0;
}

bool PgTableDesc::IsRangePartitioned() const {
  return schema().num_hash_key_columns() == 0;
}

const std::vector<std::string>& PgTableDesc::GetPartitions() const {
  return table_partitions_->keys;
}

const std::string& PgTableDesc::LastPartition() const {
  return table_partitions_->keys.back();
}

size_t PgTableDesc::GetPartitionCount() const {
  return table_partitions_->keys.size();
}

Result<string> PgTableDesc::DecodeYbctid(const Slice& ybctid) const {
  // TODO(neil) If a partition schema can have both hash and range partitioning, this function needs
  // to be updated to return appropriate primary key.
  RSTATUS_DCHECK(!IsHashPartitioned() || !IsRangePartitioned(), InvalidArgument,
                 "Partitioning schema by both hash and range is not yet supported");

  // Use range key if there's no hash columns.
  // NOTE: Also see bug github #5832.
  if (IsRangePartitioned()) {
    // Decoding using range partitioning method.
    return ybctid.ToBuffer();
  }

  // Decoding using hash partitioning method.
  // Do not check with predicate IsHashPartitioning() for now to use existing behavior by default.
  uint16 hash_code = VERIFY_RESULT(docdb::DocKey::DecodeHash(ybctid));
  return PartitionSchema::EncodeMultiColumnHashValue(hash_code);
}

Result<size_t> PgTableDesc::FindPartitionIndex(const Slice& ybctid) const {
  // Find partition index based on ybctid value.
  // - Hash Partition: ybctid -> hashcode -> key -> partition index.
  // - Range Partition: ybctid == key -> partition index.
  string partition_key = VERIFY_RESULT(DecodeYbctid(ybctid));
  return client::FindPartitionStartIndex(table_partitions_->keys, partition_key);
}

Status PgTableDesc::SetScanBoundary(LWPgsqlReadRequestPB *req,
                                    const string& partition_lower_bound,
                                    bool lower_bound_is_inclusive,
                                    const string& partition_upper_bound,
                                    bool upper_bound_is_inclusive) {
  // Setup lower boundary.
  if (!partition_lower_bound.empty()) {
    req->mutable_lower_bound()->dup_key(partition_lower_bound);
    req->mutable_lower_bound()->set_is_inclusive(lower_bound_is_inclusive);
  }

  // Setup upper boundary.
  if (!partition_upper_bound.empty()) {
    req->mutable_upper_bound()->dup_key(partition_upper_bound);
    req->mutable_upper_bound()->set_is_inclusive(upper_bound_is_inclusive);
  }

  return Status::OK();
}

const client::YBTableName& PgTableDesc::table_name() const {
  return table_name_;
}

size_t PgTableDesc::num_range_key_columns() const {
  // skip system column: ybidxbasectid/ybuniqueidxkeysuffix of INDEX/UNIQUE INDEX
  if (IsIndex()) {
    return schema().num_range_key_columns() - 1;
  }
  return schema().num_range_key_columns();
}

size_t PgTableDesc::num_hash_key_columns() const {
  return schema().num_hash_key_columns();
}

size_t PgTableDesc::num_key_columns() const {
  return schema().num_key_columns();
}

size_t PgTableDesc::num_columns() const {
  return schema().num_columns();
}

const PartitionSchema& PgTableDesc::partition_schema() const {
  return partition_schema_;
}

const Schema& PgTableDesc::schema() const {
  return schema_;
}

uint32_t PgTableDesc::schema_version() const {
  return resp_.version();
}

bool PgTableDesc::IsIndex() const {
  return resp_.has_index_info();
}

}  // namespace pggate
}  // namespace yb
