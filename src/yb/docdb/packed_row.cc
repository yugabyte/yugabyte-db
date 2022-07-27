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
#include "yb/docdb/schema_packing.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/coding_consts.h"
#include "yb/util/fast_varint.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

namespace yb {
namespace docdb {

RowPacker::RowPacker(SchemaVersion version, std::reference_wrapper<const SchemaPacking> packing)
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

bool RowPacker::Finished() const {
  return idx_ == packing_.columns();
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
  AppendEncodedValue(value, result);
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
