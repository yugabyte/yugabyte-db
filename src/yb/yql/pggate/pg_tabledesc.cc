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

#include "yb/dockv/partition.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/casts.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_client.h"

using std::string;

namespace yb {
namespace pggate {

PgTableDesc::PgTableDesc(
    const PgObjectId& relfilenode_id, const master::GetTableSchemaResponsePB& resp,
    client::VersionedTablePartitionList partition_list)
    : relfilenode_id_(relfilenode_id), resp_(resp),
      table_partition_list_(std::move(partition_list)),
      latest_known_table_partition_list_version_(table_partition_list_.version) {
  table_name_.GetFromTableIdentifierPB(resp.identifier());
}

Status PgTableDesc::Init() {
  RETURN_NOT_OK(SchemaFromPB(resp_.schema(), &schema_));
  schema_packing_.emplace(TableType::PGSQL_TABLE_TYPE, schema_);
  size_t idx = 0;
  for (const auto& column : schema().columns()) {
    attr_num_map_.emplace_back(column.order(), idx++);
  }
  std::sort(attr_num_map_.begin(), attr_num_map_.end());
  if (resp_.has_tablegroup_id()) {
    tablegroup_oid_ = VERIFY_RESULT(GetPgsqlTablegroupOid(resp_.tablegroup_id()));
  }
  if (resp_.has_pg_table_id() && !resp_.pg_table_id().empty()) {
    pg_table_id_ = VERIFY_RESULT(GetPgsqlTableOid(resp_.pg_table_id()));
  }
  return dockv::PartitionSchema::FromPB(resp_.partition_schema(), schema_, &partition_schema_);
}

struct CmpAttrNum {
  template <class P>
  bool operator()(const P& lhs, int rhs) const {
    return lhs.first < rhs;
  }
};

Result<size_t> PgTableDesc::FindColumn(int attr_num) const {
  // Find virtual columns.
  if (attr_num == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    return num_columns();
  }

  // Find physical column.
  const auto itr = std::lower_bound(
      attr_num_map_.begin(), attr_num_map_.end(), attr_num, CmpAttrNum());
  if (itr != attr_num_map_.end() && itr->first == attr_num) {
    return itr->second;
  }

  return STATUS_FORMAT(InvalidArgument, "Invalid column number $0", attr_num);
}

Result<YBCPgColumnInfo> PgTableDesc::GetColumnInfo(int attr_number) const {
  YBCPgColumnInfo column_info {
    .is_primary = false,
    .is_hash = false
  };
  const auto itr = std::lower_bound(
      attr_num_map_.begin(), attr_num_map_.end(), attr_number, CmpAttrNum());
  if (itr != attr_num_map_.end() && itr->first == attr_number) {
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

const client::TablePartitionList& PgTableDesc::GetPartitionList() const {
  return table_partition_list_.keys;
}

size_t PgTableDesc::GetPartitionListSize() const {
  return table_partition_list_.keys.size();
}

client::PartitionListVersion PgTableDesc::GetPartitionListVersion() const {
  return table_partition_list_.version;
}

void PgTableDesc::SetLatestKnownPartitionListVersion(client::PartitionListVersion version) {
  DCHECK(version >= latest_known_table_partition_list_version_);
  if (version > latest_known_table_partition_list_version_) {
    latest_known_table_partition_list_version_ = version;
  }
}

Status PgTableDesc::EnsurePartitionListIsUpToDate(PgClient* client) {
  if (table_partition_list_.version == latest_known_table_partition_list_version_) {
    return Status::OK();
  }
  DCHECK(table_partition_list_.version < latest_known_table_partition_list_version_);

  auto partition_list = VERIFY_RESULT(client->GetTablePartitionList(relfilenode_id()));
  VLOG(1) << Format(
      "Received partition list for table \"$0\", "
      "new version: $1, old version: $2, latest known version: $3.",
      table_name(), partition_list.version, table_partition_list_.version,
      latest_known_table_partition_list_version_);

  RSTATUS_DCHECK(latest_known_table_partition_list_version_ <= partition_list.version, IllegalState,
      "Unexpected version of received partition list.");

  table_partition_list_ = std::move(partition_list);
  SetLatestKnownPartitionListVersion(table_partition_list_.version);
  return Status::OK();
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
  uint16 hash_code = VERIFY_RESULT(dockv::DocKey::DecodeHash(ybctid));
  return dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
}

Result<size_t> PgTableDesc::FindPartitionIndex(const Slice& ybctid) const {
  // Find partition index based on ybctid value.
  // - Hash Partition: ybctid -> hashcode -> key -> partition index.
  // - Range Partition: ybctid == key -> partition index.
  string partition_key = VERIFY_RESULT(DecodeYbctid(ybctid));
  return client::FindPartitionStartIndex(table_partition_list_.keys, partition_key);
}

Result<bool> PgTableDesc::CheckScanBoundary(LWPgsqlReadRequestPB* req) {
  if (req->has_lower_bound() && req->has_upper_bound() &&
      ((req->lower_bound().key() > req->upper_bound().key()) ||
       (req->lower_bound().key() == req->upper_bound().key() &&
          !(req->lower_bound().is_inclusive() && req->upper_bound().is_inclusive())))) {
    return false;
  }
  return true;
}

Result<bool> PgTableDesc::SetScanBoundary(LWPgsqlReadRequestPB* req,
                                          const std::string& partition_lower_bound,
                                          bool lower_bound_is_inclusive,
                                          const std::string& partition_upper_bound,
                                          bool upper_bound_is_inclusive) {
  // Update lower boundary if necessary.
  if (!partition_lower_bound.empty()) {
    if (!req->has_lower_bound() ||
        req->lower_bound().key() < partition_lower_bound) {
      req->mutable_lower_bound()->dup_key(partition_lower_bound);
      req->mutable_lower_bound()->set_is_inclusive(lower_bound_is_inclusive);
    } else if (req->lower_bound().key() == partition_lower_bound &&
               req->lower_bound().is_inclusive() && !lower_bound_is_inclusive) {
      req->mutable_lower_bound()->set_is_inclusive(false);
    }
  }

  // Update upper boundary if necessary.
  if (!partition_upper_bound.empty()) {
    if (!req->has_upper_bound() ||
        req->upper_bound().key() > partition_upper_bound) {
      req->mutable_upper_bound()->dup_key(partition_upper_bound);
      req->mutable_upper_bound()->set_is_inclusive(upper_bound_is_inclusive);
    } else if (req->upper_bound().key() == partition_upper_bound &&
               req->upper_bound().is_inclusive() && !upper_bound_is_inclusive) {
      req->mutable_upper_bound()->set_is_inclusive(false);
    }
  }

  return CheckScanBoundary(req);
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

const dockv::PartitionSchema& PgTableDesc::partition_schema() const {
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
