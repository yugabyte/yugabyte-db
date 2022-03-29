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

#include "yb/docdb/primitive_value.h"

#include "yb/gutil/casts.h"

#include "yb/util/coding_consts.h"
#include "yb/util/fast_varint.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

bool IsVarlenColumn(const ColumnSchema& column_schema) {
  return column_schema.is_nullable() || column_schema.type_info()->var_length();
}

size_t EncodedValueSize(const ColumnSchema& column_schema) {
  return 1 + column_schema.type_info()->size;
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

size_t LoadEnd(size_t idx, const Slice& packed) {
  return LittleEndian::Load32(packed.data() + idx * sizeof(uint32_t));
}

Result<Slice> SchemaPacking::GetValue(size_t idx, const Slice& packed) const {
  const auto& column_data = columns_[idx];
  size_t offset = column_data.num_varlen_columns_before
      ? LoadEnd(column_data.num_varlen_columns_before - 1, packed) : 0;
  offset += prefix_len() + column_data.offset_after_prev_varlen_column;
  size_t end = column_data.varlen()
      ? prefix_len() + LoadEnd(column_data.num_varlen_columns_before, packed)
      : offset + column_data.size;
  return Slice(packed.data() + offset, packed.data() + end);
}

Result<Slice> SchemaPacking::GetValue(ColumnId column, const Slice& packed) const {
  auto it = column_to_idx_.find(column);
  if (it == column_to_idx_.end()) {
    return STATUS_FORMAT(InvalidArgument, "Get value for unknown column: $0", column);
  }
  return GetValue(it->second, packed);
}

RowPacker::RowPacker(uint32_t version, std::reference_wrapper<const SchemaPacking> packing)
    : packing_(packing) {
  size_t prefix_len = packing_.prefix_len();
  result_.Reserve(kMaxVarint32Length + prefix_len);
  result_.Truncate(util::FastEncodeUnsignedVarInt(version, result_.mutable_data()));
  prefix_end_ = result_.size() + prefix_len;
  varlen_write_pos_ = result_.GrowByAtLeast(prefix_len) - result_.AsSlice().cdata();
}

Status RowPacker::AddValue(ColumnId column, const QLValuePB& value, SortingType sorting_type) {
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
    AppendEncodedValue(value, sorting_type, &result_);
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

Result<const ValueBuffer&> RowPacker::Complete() {
  if (varlen_write_pos_ != prefix_end_) {
    return STATUS_FORMAT(
        InvalidArgument, "Not all varlen columns packed: $0 vs $1",
        varlen_write_pos_, prefix_end_);
  }
  const ValueBuffer& result = result_;
  return result;
}

} // namespace docdb
} // namespace yb
