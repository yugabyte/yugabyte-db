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

namespace yb {
namespace docdb {

namespace {

bool IsVarlenColumn(const ColumnSchema& column_schema) {
  return column_schema.is_nullable() || column_schema.type_info()->var_length();
}

size_t EncodedValueSize(const ColumnSchema& column_schema) {
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

SchemaPacking::SchemaPacking(const Schema& schema) {
  varlen_columns_count_ = 0;
  size_t offset_after_prev_varlen_column = 0;
  columns_.reserve(schema.num_columns() - schema.num_key_columns());
  for (auto i = schema.num_key_columns(); i != schema.num_columns(); ++i) {
    const auto& column_schema = schema.column(i);
    column_to_idx_.emplace(schema.column_id(i), columns_.size());
    bool varlen = IsVarlenColumn(column_schema);
    columns_.emplace_back(ColumnPackingData {
      .id = schema.column_id(i),
      .num_varlen_columns_before = varlen_columns_count_,
      .offset_after_prev_varlen_column = offset_after_prev_varlen_column,
      .size = varlen ? 0 : EncodedValueSize(column_schema),
      .nullable = column_schema.is_nullable(),
    });

    if (varlen) {
      ++varlen_columns_count_;
      offset_after_prev_varlen_column = 0;
    } else {
      offset_after_prev_varlen_column += EncodedValueSize(column_schema);
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
}

size_t LoadEnd(size_t idx, const Slice& packed) {
  return LittleEndian::Load32(packed.data() + idx * sizeof(uint32_t));
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

boost::optional<Slice> SchemaPacking::GetValue(ColumnId column, const Slice& packed) const {
  auto it = column_to_idx_.find(column);
  if (it == column_to_idx_.end()) {
    return boost::none;
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
}

SchemaPackingStorage::SchemaPackingStorage() = default;

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(uint32_t schema_version) const {
  auto it = version_to_schema_packing_.find(schema_version);
  if (it == version_to_schema_packing_.end()) {
    return STATUS_FORMAT(NotFound, "Schema packing not found: $0", schema_version);
  }
  return it->second;
}

Result<const SchemaPacking&> SchemaPackingStorage::GetPacking(Slice* packed_row) const {
  auto version = VERIFY_RESULT(util::FastDecodeUnsignedVarInt(packed_row));
  return GetPacking(narrow_cast<uint32_t>(version));
}

void SchemaPackingStorage::AddSchema(uint32_t version, const Schema& schema) {
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
    uint32_t skip_schema_version, google::protobuf::RepeatedPtrField<SchemaPackingPB>* out) {
  for (const auto& version_and_packing : version_to_schema_packing_) {
    if (version_and_packing.first == skip_schema_version) {
      continue;;
    }
    auto* packing_out = out->Add();
    packing_out->set_schema_version(version_and_packing.first);
    version_and_packing.second.ToPB(packing_out);
  }
}

RowPacker::RowPacker(uint32_t version, std::reference_wrapper<const SchemaPacking> packing)
    : packing_(packing) {
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

Status RowPacker::AddValue(ColumnId column, const QLValuePB& value) {
  return DoAddValue(column, value);
}

Status RowPacker::AddValue(ColumnId column, const Slice& value) {
  return DoAddValue(column, value);
}

namespace {

bool IsNull(const Slice& slice) {
  return slice.empty();
}

void PackValue(const QLValuePB& value, ValueBuffer* result) {
  AppendEncodedValue(value, CheckIsCollate::kTrue, result);
}

void PackValue(const Slice& value, ValueBuffer* result) {
  result->Append(value);
}

} // namespace

template <class Value>
Status RowPacker::DoAddValue(ColumnId column, const Value& value) {
  if (idx_ >= packing_.columns()) {
    return STATUS_FORMAT(InvalidArgument, "Add value for unknown column: $0, idx: $1",
                         column, idx_);
  }
  const auto& column_data = packing_.column_packing_data(idx_);
  if (column_data.id != column) {
    return STATUS_FORMAT(InvalidArgument, "Add value for unknown column: $0 vs $1",
                         column, column_data.id);
  }

  ++idx_;
  size_t prev_size = result_.size();
  if (!column_data.nullable || !IsNull(value)) {
    PackValue(value, &result_);
  }
  if (column_data.varlen()) {
    LittleEndian::Store32(result_.mutable_data() + varlen_write_pos_,
                          narrow_cast<uint32_t>(result_.size() - prefix_end_));
    varlen_write_pos_ += sizeof(uint32_t);
  } else if (prev_size + column_data.size != result_.size()) {
    return STATUS_FORMAT(Corruption, "Wrong encoded size: $0 vs $1",
                         result_.size() - prev_size, column_data.size);
  }

  return Status::OK();
}

Result<Slice> RowPacker::Complete() {
  if (idx_ != packing_.columns()) {
    return STATUS_FORMAT(
        InvalidArgument, "Not all columns packed: $0 vs $1",
        idx_, packing_.columns());
  }
  if (varlen_write_pos_ != prefix_end_) {
    return STATUS_FORMAT(
        InvalidArgument, "Not all varlen columns packed: $0 vs $1",
        varlen_write_pos_, prefix_end_);
  }
  return result_.AsSlice();
}

ColumnId RowPacker::NextColumnId() const {
  return idx_ < packing_.columns() ? packing_.column_packing_data(idx_).id : kInvalidColumnId;
}

Result<const ColumnPackingData&> RowPacker::NextColumnData() const {
  if (idx_ >= packing_.columns()) {
    return STATUS(IllegalState, "All columns already packed");
  }
  return packing_.column_packing_data(idx_);
}

} // namespace docdb
} // namespace yb
