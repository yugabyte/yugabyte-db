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

#include "yb/docdb/schema_packing.h"

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_wire_protocol.h"
#include "yb/common/schema.h"

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/primitive_value.h"

#include "yb/gutil/casts.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/fast_varint.h"

DEFINE_test_flag(bool, dcheck_for_missing_schema_packing, true,
                 "Whether we use check failure for missing schema packing in debug builds");

namespace yb {
namespace docdb {

namespace {

// Used to mark column as skipped by packer. For instance in case of collection column.
constexpr int64_t kSkippedColumnIdx = -1;

bool IsVarlenColumn(TableType table_type, const ColumnSchema& column_schema) {
  // CQL columns could have individual TTL.
  return table_type == TableType::YQL_TABLE_TYPE ||
         column_schema.is_nullable() || column_schema.type_info()->var_length();
}

size_t EncodedColumnSize(const ColumnSchema& column_schema) {
  if (column_schema.type_info()->type == DataType::BOOL) {
    // Boolean values are encoded as value type only.
    return 1;
  }
  return 1 + column_schema.type_info()->size;
}

} // namespace

ColumnPackingData ColumnPackingData::FromPB(const ColumnPackingPB& pb) {
  return ColumnPackingData {
    .id = ColumnId(pb.id()),
    .num_varlen_columns_before = pb.num_varlen_columns_before(),
    .offset_after_prev_varlen_column = pb.offset_after_prev_varlen_column(),
    .size = pb.size(),
    .nullable = pb.nullable(),
  };
}

void ColumnPackingData::ToPB(ColumnPackingPB* out) const {
  out->set_id(id.rep());
  out->set_num_varlen_columns_before(num_varlen_columns_before);
  out->set_offset_after_prev_varlen_column(offset_after_prev_varlen_column);
  out->set_size(size);
  out->set_nullable(nullable);
}

std::string ColumnPackingData::ToString() const {
  return YB_STRUCT_TO_STRING(
      id, num_varlen_columns_before, offset_after_prev_varlen_column, size, nullable);
}

SchemaPacking::SchemaPacking(TableType table_type, const Schema& schema) {
  varlen_columns_count_ = 0;
  size_t offset_after_prev_varlen_column = 0;
  columns_.reserve(schema.num_columns() - schema.num_key_columns());
  for (auto i = schema.num_key_columns(); i != schema.num_columns(); ++i) {
    const auto& column_schema = schema.column(i);
    auto column_id = schema.column_id(i);
    if (column_schema.is_collection() || column_schema.is_static()) {
      column_to_idx_.emplace(column_id, kSkippedColumnIdx);
      continue;
    }
    column_to_idx_.emplace(column_id, columns_.size());
    bool varlen = IsVarlenColumn(table_type, column_schema);
    columns_.emplace_back(ColumnPackingData {
      .id = schema.column_id(i),
      .num_varlen_columns_before = varlen_columns_count_,
      .offset_after_prev_varlen_column = offset_after_prev_varlen_column,
      .size = varlen ? 0 : EncodedColumnSize(column_schema),
      .nullable = column_schema.is_nullable(),
    });

    if (varlen) {
      ++varlen_columns_count_;
      offset_after_prev_varlen_column = 0;
    } else {
      offset_after_prev_varlen_column += EncodedColumnSize(column_schema);
    }
  }
}

SchemaPacking::SchemaPacking(const SchemaPackingPB& pb) : varlen_columns_count_(0) {
  columns_.reserve(pb.columns().size());
  for (const auto& entry : pb.columns()) {
    columns_.push_back(ColumnPackingData::FromPB(entry));
    column_to_idx_.emplace(columns_.back().id, columns_.size() - 1);
    if (columns_.back().varlen()) {
      ++varlen_columns_count_;
    }
  }
  for (auto skipped_column_id : pb.skipped_column_ids()) {
    column_to_idx_.emplace(skipped_column_id, kSkippedColumnIdx);
  }
}

size_t LoadEnd(size_t idx, const Slice& packed) {
  return LittleEndian::Load32(packed.data() + idx * sizeof(uint32_t));
}

bool SchemaPacking::SkippedColumn(ColumnId column_id) const {
  auto it = column_to_idx_.find(column_id);
  return it != column_to_idx_.end() && it->second == kSkippedColumnIdx;
}

Slice SchemaPacking::GetValue(size_t idx, const Slice& packed) const {
  const auto& column_data = columns_[idx];
  size_t offset = column_data.num_varlen_columns_before
      ? LoadEnd(column_data.num_varlen_columns_before - 1, packed) : 0;
  offset += prefix_len() + column_data.offset_after_prev_varlen_column;
  size_t end = column_data.varlen()
      ? prefix_len() + LoadEnd(column_data.num_varlen_columns_before, packed)
      : offset + column_data.size;
  return Slice(packed.data() + offset, packed.data() + end);
}

std::optional<Slice> SchemaPacking::GetValue(ColumnId column_id, const Slice& packed) const {
  auto it = column_to_idx_.find(column_id);
  if (it == column_to_idx_.end() || it->second == kSkippedColumnIdx) {
    return {};
  }
  return GetValue(it->second, packed);
}

std::string SchemaPacking::ToString() const {
  return YB_CLASS_TO_STRING(columns, varlen_columns_count);
}

void SchemaPacking::ToPB(SchemaPackingPB* out) const {
  for (const auto& column : columns_) {
    column.ToPB(out->add_columns());
  }
  for (const auto& [column_id, column_idx] : column_to_idx_) {
    if (column_idx == kSkippedColumnIdx) {
      out->add_skipped_column_ids(column_id);
    }
  }
}

bool SchemaPacking::CouldPack(
    const google::protobuf::RepeatedPtrField<QLColumnValuePB>& values) const {
  if (make_unsigned(values.size()) != column_to_idx_.size()) {
    return false;
  }
  for (const auto& value : values) {
    if (!column_to_idx_.contains(ColumnId(value.column_id()))) {
      return false;
    }
  }
  return true;
}

SchemaPackingStorage::SchemaPackingStorage(TableType table_type) : table_type_(table_type) {}

SchemaPackingStorage::SchemaPackingStorage(
    const SchemaPackingStorage& rhs, SchemaVersion min_schema_version)
    : table_type_(rhs.table_type_) {
  for (const auto& [version, packing] : rhs.version_to_schema_packing_) {
    if (version < min_schema_version) {
      continue;
    }
    version_to_schema_packing_.emplace(version, packing);
  }
}

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(SchemaVersion schema_version) const {
  auto it = version_to_schema_packing_.find(schema_version);
  auto get_first = [](const auto& pair) { return pair.first; };
  if (it == version_to_schema_packing_.end()) {
    auto status = STATUS_FORMAT(
        NotFound, "Schema packing not found: $0, available_versions: $1",
        schema_version, CollectionToString(version_to_schema_packing_, get_first));
#ifndef NDEBUG
    if (FLAGS_TEST_dcheck_for_missing_schema_packing) {
      CHECK_OK(status);
    }
#endif
    return status;
  }
  return it->second;
}

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(Slice* packed_row) const {
  auto version = VERIFY_RESULT(util::FastDecodeUnsignedVarInt(packed_row));
  return GetPacking(narrow_cast<SchemaVersion>(version));
}

void SchemaPackingStorage::AddSchema(SchemaVersion version, const Schema& schema) {
  auto inserted = version_to_schema_packing_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(version),
      std::forward_as_tuple(table_type_, schema)).second;
  LOG_IF(DFATAL, !inserted)
      << "Duplicate schema version: " << version << ", " << AsString(version_to_schema_packing_);
}

Status SchemaPackingStorage::LoadFromPB(
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas) {
  version_to_schema_packing_.clear();
  return InsertSchemas(schemas, false, OverwriteSchemaPacking::kFalse);
}

Status SchemaPackingStorage::MergeWithRestored(
    SchemaVersion schema_version, const SchemaPB& schema,
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
    OverwriteSchemaPacking overwrite) {
  RETURN_NOT_OK(InsertSchemas(schemas, true, overwrite));
  if (overwrite) {
    version_to_schema_packing_.erase(schema_version);
  }
  if (!version_to_schema_packing_.contains(schema_version)) {
    Schema temp_schema;
    RETURN_NOT_OK(SchemaFromPB(schema, &temp_schema));
    AddSchema(schema_version, temp_schema);
  }
  if (VLOG_IS_ON(2)) {
    VLOG(2) << "Schema Packing History after merging with snapshot";
    for (const auto& [version, packing] : version_to_schema_packing_) {
      VLOG(2) << version << " : " << packing.ToString();
    }
  }
  return Status::OK();
}

Status SchemaPackingStorage::InsertSchemas(
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas,
    bool could_present, OverwriteSchemaPacking overwrite) {
  for (const auto& entry : schemas) {
    if (overwrite) {
      version_to_schema_packing_.erase(entry.schema_version());
    }
    auto inserted = version_to_schema_packing_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(entry.schema_version()),
        std::forward_as_tuple(entry)).second;
    RSTATUS_DCHECK(could_present || inserted, Corruption,
                   Format("Duplicate schema version: $0", entry.schema_version()));
  }
  return Status::OK();
}

void SchemaPackingStorage::ToPB(
    SchemaVersion skip_schema_version, google::protobuf::RepeatedPtrField<SchemaPackingPB>* out) {
  for (const auto& version_and_packing : version_to_schema_packing_) {
    if (version_and_packing.first == skip_schema_version) {
      continue;
    }
    auto* packing_out = out->Add();
    packing_out->set_schema_version(version_and_packing.first);
    version_and_packing.second.ToPB(packing_out);
  }
}

bool SchemaPackingStorage::HasVersionBelow(SchemaVersion version) const {
  for (const auto& p : version_to_schema_packing_) {
    if (p.first < version) {
      return true;
    }
  }
  return false;
}

std::string SchemaPackingStorage::VersionsToString() const {
  std::vector<SchemaVersion> versions;
  versions.reserve(version_to_schema_packing_.size());
  for (const auto& [version, schema] : version_to_schema_packing_) {
    versions.push_back(version);
  }
  std::sort(versions.begin(), versions.end());
  return AsString(versions);
}

} // namespace docdb
} // namespace yb
