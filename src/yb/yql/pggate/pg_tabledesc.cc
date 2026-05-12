//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/tostring.h"

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
  schema_packing_.emplace(yb::TableType::PGSQL_TABLE_TYPE, schema_);
  size_t idx = 0;
  for (const auto& column : schema().columns()) {
    attr_num_map_.emplace_back(column.order(), idx++);
  }
  std::sort(attr_num_map_.begin(), attr_num_map_.end());
  // Diagnostic for schema-staleness investigations (e.g. "Invalid column number N" from
  // FindColumn). Enable with `--vmodule=pg_tabledesc=1` to see exactly which schema each
  // pggate session bound into its attr_num_map_, including the response schema_version and
  // the full GetTableSchemaResponsePB this PgTableDesc was built from.
  VLOG(1) << "PgTableDesc::Init relfilenode=" << relfilenode_id_.ToString()
          << " schema_version=" << resp_.version()
          << " num_columns=" << resp_.schema().columns_size()
          << " attr_num_map=" << yb::ToString(attr_num_map_)
          << " resp=" << resp_.ShortDebugString();
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

  // Special case: if kYBIdxBaseTupleId is not in the attr_num_map_, treat it as
  // kYBTupleId. This allows us to easily request ybctids on secondary indexes.
  if (attr_num == static_cast<int>(PgSystemAttrNum::kYBIdxBaseTupleId))
    return num_columns();

  // Always log the schema we are searching against when FindColumn fails. This error path
  // surfaces to PG as `XX000 ERROR: Invalid column number N` and is non-retryable; capturing
  // the bound schema here is the only way to retroactively distinguish a stale-schema race
  // from an attnum-vs-order conversion bug. See investigation of #20327 / customer report.
  LOG(WARNING) << "PgTableDesc::FindColumn failed: relfilenode=" << relfilenode_id_.ToString()
               << " pg_table_id=" << pg_table_id().ToString()
               << " table=" << table_name_.ToString(/*include_id=*/false)
               << " requested_attr_num=" << attr_num
               << " schema_version=" << resp_.version()
               << " num_columns=" << resp_.schema().columns_size()
               << " attr_num_map=" << yb::ToString(attr_num_map_);
  // Embed the table identity in the user-visible Status. The bare "Invalid column number N"
  // message that PG surfaces as `XX000 ERROR:` was untraceable in production: the customer
  // report had no way to identify which table the failing query referenced. Both identifiers
  // are included because they diverge on TRUNCATE/REWRITE -- relfilenode_id is the DocDB-side
  // id that rotates, pg_table_id is the stable PG `pg_class.oid`. The "Invalid column number
  // N" prefix is preserved so existing test classifiers (pg_concurrent_ddl_invalid_column,
  // pg_packed_row, pg_ddl_atomicity_stress, pg_single_tserver, TestSchemaVersionMismatch,
  // TestPgDdlConcurrency) that match on that substring continue to work.
  return STATUS_FORMAT(
      InvalidArgument,
      "Invalid column number $0 (table=$1, relfilenode=$2, pg_table_id=$3, "
      "schema_version=$4, num_columns=$5)",
      attr_num,
      table_name_.ToString(/*include_id=*/false),
      relfilenode_id_.ToString(),
      pg_table_id().ToString(),
      resp_.version(),
      num_columns());
}

const std::vector<std::pair<int, size_t>>& PgTableDesc::GetAttrNumMap() const {
  return attr_num_map_;
}

Result<YbcPgColumnInfo> PgTableDesc::GetColumnInfo(int attr_number) const {
  YbcPgColumnInfo column_info {
    .is_key = false,
    .is_hash = false
  };
  const auto itr = std::lower_bound(
      attr_num_map_.begin(), attr_num_map_.end(), attr_number, CmpAttrNum());
  if (itr != attr_num_map_.end() && itr->first == attr_number) {
    column_info.is_key = itr->second < schema().num_key_columns();
    column_info.is_hash = itr->second < schema().num_hash_key_columns();
  }
  return column_info;
}

bool PgTableDesc::IsColocated() const {
  return resp_.colocated();
}

YbcPgOid PgTableDesc::GetColocationId() const {
  return schema().has_colocation_id() ? schema().colocation_id() : kColocationIdNotSet;
}

YbcPgOid PgTableDesc::GetTablegroupOid() const {
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
