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
#include "yb/docdb/value.h"
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

size_t PackedSizeLimit(size_t value) {
  return value ? value : make_unsigned(FLAGS_db_block_size_bytes);
}

} // namespace

RowPacker::RowPacker(
    SchemaVersion version, std::reference_wrapper<const SchemaPacking> packing,
    size_t packed_size_limit, const ValueControlFields& control_fields)
    : packing_(packing),
      packed_size_limit_(PackedSizeLimit(packed_size_limit)) {
  control_fields.AppendEncoded(&result_);
  Init(version);
}

RowPacker::RowPacker(
    SchemaVersion version, std::reference_wrapper<const SchemaPacking> packing,
    size_t packed_size_limit, const Slice& control_fields)
    : packing_(packing),
      packed_size_limit_(PackedSizeLimit(packed_size_limit)) {
  result_.Append(control_fields);
  Init(version);
}

void RowPacker::Init(SchemaVersion version) {
  size_t prefix_len = packing_.prefix_len();
  result_.Reserve(result_.size() + 1 + kMaxVarint32Length + prefix_len);
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
  // In case of concurrent schema change YSQL does not send recently added columns.
  // Fill them with NULLs to keep the same behaviour like we have w/o packed row.
  while (idx_ < packing_.columns()) {
    RETURN_NOT_OK(AddValue(packing_.column_packing_data(idx_).id, Slice(), 0));
  }
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
