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

#include "yb/docdb/packed_row.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/coding_consts.h"
#include "yb/util/fast_varint.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

DECLARE_int64(db_block_size_bytes);

namespace yb {
namespace docdb {

namespace {

// Used to mark column as skipped by packer. For instance in case of collection column.
constexpr int64_t kSkippedColumnIdx = -1;

bool IsVarlenColumn(const ColumnSchema& column_schema) {
  return column_schema.is_nullable() || column_schema.type_info()->var_length();
}

size_t EncodedColumnSize(const ColumnSchema& column_schema) {
  if (column_schema.type_info()->type == DataType::BOOL) {
    // Boolean values are encoded as value type only.
    return 1;
  }
  return 1 + column_schema.type_info()->size;
}

bool IsNull(const Slice& slice) {
  return slice.empty();
}

void PackValue(const QLValuePB& value, ValueBuffer* result) {
  AppendEncodedValue(value, result);
}

size_t PackedValueSize(const QLValuePB& value) {
  return EncodedValueSize(value);
}

void PackValue(const Slice& value, ValueBuffer* result) {
  result->Append(value);
}

size_t PackedValueSize(const Slice& value) {
  return value.size();
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

SchemaPacking::SchemaPacking(const Schema& schema) {
  varlen_columns_count_ = 0;
  size_t offset_after_prev_varlen_column = 0;
  columns_.reserve(schema.num_columns() - schema.num_key_columns());
  for (auto i = schema.num_key_columns(); i != schema.num_columns(); ++i) {
    const auto& column_schema = schema.column(i);
    auto column_id = schema.column_id(i);
    if (column_schema.is_collection()) {
      column_to_idx_.emplace(column_id, kSkippedColumnIdx);
      continue;
    }
    column_to_idx_.emplace(column_id, columns_.size());
    bool varlen = IsVarlenColumn(column_schema);
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

SchemaPackingStorage::SchemaPackingStorage() = default;

SchemaPackingStorage::SchemaPackingStorage(
    const SchemaPackingStorage& rhs, SchemaVersion min_schema_version) {
  for (const auto& [version, packing] : rhs.version_to_schema_packing_) {
    if (version < min_schema_version) {
      continue;
    }
    version_to_schema_packing_.emplace(version, packing);
  }
}

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(SchemaVersion schema_version) const {
  auto it = version_to_schema_packing_.find(schema_version);
  if (it == version_to_schema_packing_.end()) {
    return STATUS_FORMAT(NotFound, "Schema packing not found: $0", schema_version);
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
      std::forward_as_tuple(schema)).second;
  LOG_IF(DFATAL, !inserted)
      << "Duplicate schema version: " << version << ", " << AsString(version_to_schema_packing_);
}

Status SchemaPackingStorage::LoadFromPB(
    const google::protobuf::RepeatedPtrField<SchemaPackingPB>& schemas) {
  version_to_schema_packing_.clear();
  for (const auto& entry : schemas) {
    auto inserted = version_to_schema_packing_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(entry.schema_version()),
        std::forward_as_tuple(entry)).second;
    RSTATUS_DCHECK(inserted, Corruption,
                   Format("Duplicate schema version: $0", entry.schema_version()));
  }
  return Status::OK();
}

void SchemaPackingStorage::ToPB(
    SchemaVersion skip_schema_version, google::protobuf::RepeatedPtrField<SchemaPackingPB>* out) {
  for (const auto& version_and_packing : version_to_schema_packing_) {
    if (version_and_packing.first == skip_schema_version) {
      continue;;
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

RowPacker::RowPacker(
    SchemaVersion version, std::reference_wrapper<const SchemaPacking> packing,
    size_t packed_size_limit)
    : packing_(packing),
      packed_size_limit_(packed_size_limit ? packed_size_limit : FLAGS_db_block_size_bytes) {
  size_t prefix_len = packing_.prefix_len();
  result_.Reserve(1 + kMaxVarint32Length + prefix_len);
  result_.PushBack(ValueEntryTypeAsChar::kPackedRow);
  result_.Truncate(
      result_.size() +
      util::FastEncodeUnsignedVarInt(version, result_.mutable_data() + result_.size()));
  result_.GrowByAtLeast(prefix_len);
  prefix_end_ = result_.size();
  varlen_write_pos_ = prefix_end_ - prefix_len;
}

void RowPacker::Restart() {
  idx_ = 0;
  varlen_write_pos_ = prefix_end_ - packing_.prefix_len();
  result_.Truncate(prefix_end_);
}

Result<bool> RowPacker::AddValue(ColumnId column_id, const QLValuePB& value) {
  return DoAddValue(column_id, value, 0);
}

Result<bool> RowPacker::AddValue(ColumnId column_id, const Slice& value, ssize_t tail_size) {
  return DoAddValue(column_id, value, tail_size);
}

template <class Value>
Result<bool> RowPacker::DoAddValue(ColumnId column_id, const Value& value, ssize_t tail_size) {
  RSTATUS_DCHECK(
      idx_ < packing_.columns(),
      InvalidArgument, "Add extra column $0, while already have $1 of $2 columns",
      column_id, idx_, packing_.columns());

  bool result = true;
  for (;;) {
    const auto& column_data = packing_.column_packing_data(idx_);
    RSTATUS_DCHECK(
        column_data.id <= column_id, InvalidArgument,
        "Add unexpected column $0, while $1 is expected", column_id, column_data.id);

    ++idx_;
    size_t prev_size = result_.size();
    if (column_data.id < column_id) {
      RSTATUS_DCHECK(
          column_data.nullable, InvalidArgument,
          "Missing value for non nullable column $0, while adding $1", column_data.id, column_id);
    } else if (!column_data.nullable || !IsNull(value)) {
      if (column_data.varlen() &&
          make_signed(prev_size + PackedValueSize(value)) + tail_size > packed_size_limit_) {
        result = false;
      } else {
        PackValue(value, &result_);
      }
    }
    if (column_data.varlen()) {
      LittleEndian::Store32(result_.mutable_data() + varlen_write_pos_,
                            narrow_cast<uint32_t>(result_.size() - prefix_end_));
      varlen_write_pos_ += sizeof(uint32_t);
    } else {
      RSTATUS_DCHECK(
          prev_size + column_data.size == result_.size(), Corruption,
          "Wrong encoded size: $0 vs $1", result_.size() - prev_size, column_data.size);
    }

    if (column_data.id == column_id) {
      break;
    }
  }

  return result;
}

Result<Slice> RowPacker::Complete() {
  RSTATUS_DCHECK_EQ(
      idx_, packing_.columns(), InvalidArgument, "Not all columns packed");
  RSTATUS_DCHECK_EQ(
      varlen_write_pos_, prefix_end_, InvalidArgument, "Not all varlen columns packed");
  return result_.AsSlice();
}

ColumnId RowPacker::NextColumnId() const {
  return idx_ < packing_.columns() ? packing_.column_packing_data(idx_).id : kInvalidColumnId;
}

Result<const ColumnPackingData&> RowPacker::NextColumnData() const {
  RSTATUS_DCHECK(
      idx_ < packing_.columns(), IllegalState, "All columns already packed");
  return packing_.column_packing_data(idx_);
}

} // namespace docdb
} // namespace yb
