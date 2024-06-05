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

#include "yb/dockv/packed_row.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/packed_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_packing_v2.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"

#include "yb/util/coding_consts.h"
#include "yb/util/fast_varint.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

using namespace yb::size_literals;

DEFINE_UNKNOWN_int64(db_block_size_bytes, 32_KB, "Size of RocksDB data block (in bytes).");

namespace yb::dockv {

namespace {

using PackedValueWithPrefixV1 = std::pair<Slice, PackedValueV1>;
using PackedValueWithPrefixV2 = std::pair<Slice, PackedValueV2>;
using ValuePair = std::pair<Slice, const QLValuePB&>;

std::string ValueToString(const QLValuePB& value) {
  return value.ShortDebugString();
}

std::string ValueToString(const LWQLValuePB& value) {
  return value.ShortDebugString();
}

std::string ValueToString(const ValuePair& value) {
  auto result = value.second.ShortDebugString();
  if (!value.first.empty()) {
    Slice control_fields_slice = value.first;
    auto control_fields = ValueControlFields::Decode(&control_fields_slice);
    result += AsString(control_fields);
  }
  return result;
}

std::string ValueToString(PackedValueV1 value) {
  return value->ToDebugHexString();
}

std::string ValueToString(PackedValueV2 value) {
  return value->ToDebugHexString();
}

std::string ValueToString(const PackableValue& value) {
  return value.ToString();
}

std::string ValueToString(const PackedValueWithPrefixV1& value) {
  if (value.first.empty()) {
    return value.second->ToDebugHexString();
  }
  return value.first.ToDebugHexString() + "+" + value.second->ToDebugHexString();
}

bool IsNull(PackedValueV1 value) {
  return value.IsNull();
}

bool IsNull(PackedValueV2 value) {
  return value.IsNull();
}

// Prefixes are used in packed row V1 for control flags specific for this particular column.
// Packed row V2 is created for YSQL only, so it does not have column specific control flags.
size_t PrefixSize(std::nullptr_t) {
  return 0;
}

void PackPrefix(std::nullptr_t, ValueBuffer* buffer) {
}

size_t PrefixSize(Slice prefix) {
  return prefix.size();
}

void PackPrefix(Slice prefix, ValueBuffer* buffer) {
  buffer->Append(prefix);
}

bool IsNull(const PackableValue& value) {
  return value.IsNull();
}

class ColumnPackerV1 {
 public:
  ColumnPackerV1(const ColumnPackingData& column_data, ValueBuffer* buffer)
      : column_data_(column_data), buffer_(*buffer) {}

  template <class Value>
  Status UpdateHeader(
      size_t prefix_end, size_t var_header_start, size_t prev_size, size_t idx,
      const Value& value) {
    if (column_data_.varlen()) {
      LittleEndian::Store32(
          buffer_.mutable_data() + var_header_start +
              column_data_.num_varlen_columns_before * sizeof(uint32_t),
          narrow_cast<uint32_t>(buffer_.size() - prefix_end));
    } else {
      RSTATUS_DCHECK(
          prev_size + column_data_.size == buffer_.size(), Corruption,
          "Wrong encoded size: $0, column: $1, value: $2",
          buffer_.size() - prev_size, column_data_, ValueToString(value));
    }

    return Status::OK();
  }

  bool PackValue(const QLValuePB& value, size_t limit) {
    return DoPackValue(/* prefix= */ nullptr, value, limit);
  }

  bool PackValue(const LWQLValuePB& value, size_t limit) {
    return DoPackValue(/* prefix= */ nullptr, value, limit);
  }

  bool PackValue(PackedValueV1 value, size_t limit) {
    return DoPackValue(/* prefix= */ nullptr, value, limit);
  }

  bool PackValue(const PackableValue& value, size_t limit) {
    return DoPackValue(/* prefix= */ nullptr, value, limit);
  }

  Result<bool> PackValue(PackedValueV2 value, size_t limit) {
    return PackValue(VERIFY_RESULT(UnpackQLValue(value, column_data_.data_type)), limit);
  }

  template <class Prefix, class Value>
  auto PackValue(const std::pair<Prefix, Value>& value, size_t limit) {
    return DoPackValue(value.first, value.second, limit);
  }

 private:
  size_t PackedValueSize(const QLValuePB& value) {
    return EncodedValueSize(value);
  }

  void DoPackValueImpl(const QLValuePB& value) {
    AppendEncodedValue(value, &buffer_);
  }

  size_t PackedValueSize(const LWQLValuePB& value) {
    return EncodedValueSize(value);
  }

  void DoPackValueImpl(const LWQLValuePB& value) {
    AppendEncodedValue(value, &buffer_);
  }

  size_t PackedValueSize(PackedValueV1 value) {
    return value->size();
  }

  void DoPackValueImpl(PackedValueV1 value) {
    buffer_.Append(*value);
  }

  size_t PackedValueSize(const PackableValue& value) {
    return value.PackedSizeV1();
  }

  void DoPackValueImpl(const PackableValue& value) {
    value.PackToV1(&buffer_);
  }

  template <class Prefix, class Value>
  bool DoPackValue(const Prefix& prefix, const Value& value, size_t limit) {
    if (IsNull(value)) {
      return true;
    }
    if (PrefixSize(prefix) + PackedValueSize(value) > limit && column_data_.varlen()) {
      return false;
    }
    PackPrefix(prefix, &buffer_);
    DoPackValueImpl(value);
    return true;
  }

  const ColumnPackingData& column_data_;
  ValueBuffer& buffer_;
};

class ColumnPackerV2 {
 public:
  ColumnPackerV2(const ColumnPackingData& column_data, ValueBuffer* buffer)
      : data_type_(column_data.data_type), buffer_(*buffer) {}

  template <class Value>
  Status UpdateHeader(
      size_t prefix_end, size_t var_header_start, size_t prev_size, size_t idx,
      const Value& value) {
    if (prev_size != buffer_.size()) {
      return Status::OK();
    }
    // We don't pack anything only if the column's value is NULL.
    MarkColumnNull(var_header_start, idx);
    return Status::OK();
  }

  bool PackValue(const QLValuePB& value, size_t limit) {
    return DoPackValue(value, limit);
  }

  bool PackValue(const LWQLValuePB& value, size_t limit) {
    return DoPackValue(value, limit);
  }

  bool PackValue(const PackableValue& value, size_t limit) {
    return DoPackValue(value, limit);
  }

  bool PackValue(PackedValueV2 value, size_t limit) {
    return DoPackValue(value, limit);
  }

  Result<bool> PackValue(PackedValueV1 value, size_t limit) {
    // TODO(packed_row) direct repacking
    return PackValue(VERIFY_RESULT(UnpackQLValue(value, data_type_)), limit);
  }

  template <class Value>
  auto PackValue(const std::pair<Slice, Value>& value, size_t limit) {
    // Drop control flags when present.
    return PackValue(value.second, limit);
  }

 private:
  size_t PackedValueSize(const QLValuePB& value) {
    return PackedQLValueSizeV2(value, data_type_);
  }

  void DoPackValueImpl(const QLValuePB& value) {
    PackQLValueV2(value, data_type_, &buffer_);
  }

  size_t PackedValueSize(const LWQLValuePB& value) {
    return PackedQLValueSizeV2(value, data_type_);
  }

  void DoPackValueImpl(const LWQLValuePB& value) {
    PackQLValueV2(value, data_type_, &buffer_);
  }

  size_t PackedValueSize(PackedValueV2 value) {
    return value->size();
  }

  void DoPackValueImpl(PackedValueV2 value) {
    buffer_.Append(*value);
  }

  size_t PackedValueSize(const PackableValue& value) {
    return value.PackedSizeV2();
  }

  void DoPackValueImpl(const PackableValue& value) {
    value.PackToV2(&buffer_);
  }

  void MarkColumnNull(size_t var_header_start, size_t idx) {
    buffer_.mutable_data()[var_header_start + idx / 8] |= 1 << (idx & 7);
  }

  template <class Value>
  bool DoPackValue(const Value& value, size_t limit) {
    if (IsNull(value)) {
      return true;
    }
    auto packed_size = PackedValueSize(value);
    if (packed_size > limit) {
      return false;
    }
    if (PackedAsVarlen(data_type_)) {
      auto* out = buffer_.GrowByAtLeast(kMaxFieldLengthSize + packed_size);
      out = EncodeFieldLength(narrow_cast<uint32_t>(packed_size), out);
      buffer_.Truncate(pointer_cast<uint8_t*>(out) - buffer_.data());
    }
    DoPackValueImpl(value);
    return true;
  }

  DataType data_type_;
  ValueBuffer& buffer_;
};

template <class Packer>
Status CompleteColumns(size_t packed_columns, Packer* packer) {
  // In case of concurrent schema change YSQL does not send recently added columns.
  // Fill them with missing default values (if any) or NULLs to keep the same behaviour
  // like we have w/o packed row.
  const auto& packing = packer->packing();
  if (packed_columns < packing.columns()) {
    const auto& packing_data = packing.column_packing_data(packing.columns() - 1);
    const auto& missing_value = VERIFY_RESULT_REF(
        packer->missing_value_provider().GetMissingValueByColumnId(packing_data.id));
    if (!IsNull(missing_value)) {
      RETURN_NOT_OK(packer->AddValue(packing_data.id, missing_value));
    } else {
      RSTATUS_DCHECK(
          packing_data.nullable, InvalidArgument, "Non nullable column $0 was not specified",
          packing_data);
      RETURN_NOT_OK(packer->AddValue(
          packing_data.id, Packer::PackedValue::Null(), /* tail_size= */ 0));
    }
  }
  return Status::OK();
}

} // namespace

size_t PackedSizeLimit(size_t value) {
  return value ? value : make_unsigned(FLAGS_db_block_size_bytes);
}

RowPackerBase::RowPackerBase(
    std::reference_wrapper<const SchemaPacking> packing, size_t packed_size_limit,
    const ValueControlFields& control_fields,
    std::reference_wrapper<const MissingValueProvider> missing_value_provider)
    : packing_(packing),
      packed_size_limit_(PackedSizeLimit(packed_size_limit)),
      missing_value_provider_(missing_value_provider) {
  control_fields.AppendEncoded(&result_);
}

RowPackerBase::RowPackerBase(
    std::reference_wrapper<const SchemaPacking> packing, size_t packed_size_limit,
    Slice control_fields,
    std::reference_wrapper<const MissingValueProvider> missing_value_provider)
    : packing_(packing),
      packed_size_limit_(PackedSizeLimit(packed_size_limit)),
      missing_value_provider_(missing_value_provider) {
  result_.Append(control_fields);
}

bool RowPackerBase::Finished() const {
  return idx_ == packing_.columns();
}

ColumnId RowPackerBase::NextColumnId() const {
  return idx_ < packing_.columns() ? packing_.column_packing_data(idx_).id : kInvalidColumnId;
}

Result<const ColumnPackingData&> RowPackerBase::NextColumnData() const {
  RSTATUS_DCHECK_LT(idx_, packing_.columns(), IllegalState, "All columns already packed");
  return packing_.column_packing_data(idx_);
}

template <class ColumnPacker, class Value>
Result<bool> RowPackerBase::DoAddValueImpl(
    ColumnId column_id, const Value& value, ssize_t tail_size) {
  VLOG_WITH_FUNC(4)
      << "column_id: " << column_id << ", value: " << ValueToString(value) << ", tail_size: "
      << tail_size;

  if (idx_ >= packing_.columns()) {
    RSTATUS_DCHECK(
        packing_.SkippedColumn(column_id),
        InvalidArgument, "Add extra column $0, while already have $1 of $2 columns",
        column_id, idx_, packing_.columns());
    return false;
  }

  bool result = true;
  for (;;) {
    const auto& column_data = packing_.column_packing_data(idx_);
    if (column_data.id > column_id) {
      RSTATUS_DCHECK(
          packing_.SkippedColumn(column_id), InvalidArgument,
          "Add unexpected column $0, while $1 is expected", column_id, column_data.id);
      return false;
    }

    ColumnPacker column_packer(column_data, &result_);
    size_t prev_size = result_.size();
    if (column_data.id < column_id) {
      RSTATUS_DCHECK(
          column_data.nullable, InvalidArgument,
          "Missing value for non nullable column $0, while adding $1", column_data.id, column_id);
    } else {
      result = OPTIONAL_VERIFY_RESULT(column_packer.PackValue(
          value, packed_size_limit_ - prev_size - tail_size));
    }
    RETURN_NOT_OK(column_packer.UpdateHeader(
        prefix_end_, var_header_start_, prev_size, idx_, value));
    ++idx_;
    if (column_data.id == column_id) {
      break;
    }
  }

  return result;
}

void RowPackerV1::Init(SchemaVersion version) {
  size_t prefix_len = packing_.prefix_len();
  auto* out = result_.GrowByAtLeast(1 + kMaxVarint32Length + prefix_len);
  *out++ = ValueEntryTypeAsChar::kPackedRowV1;
  out += FastEncodeUnsignedVarInt(version, out);
  var_header_start_ = out - pointer_cast<char*>(result_.mutable_data());
  prefix_end_ = var_header_start_ + prefix_len;
  result_.Truncate(prefix_end_);
}

Result<bool> RowPackerV1::AddValue(ColumnId column_id, const QLValuePB& value) {
  return DoAddValue(column_id, value, /* tail_size= */ 0);
}

Result<bool> RowPackerV1::AddValue(ColumnId column_id, const LWQLValuePB& value) {
  return DoAddValue(column_id, value, /* tail_size= */ 0);
}

Result<bool> RowPackerV1::AddValue(
    ColumnId column_id, Slice control_fields, const QLValuePB& value) {
  return DoAddValue(column_id, ValuePair(control_fields, value), /* tail_size= */ 0);
}

Result<bool> RowPackerV1::AddValue(ColumnId column_id, PackedValueV1 value, ssize_t tail_size) {
  return DoAddValue(column_id, value, tail_size);
}

Result<bool> RowPackerV1::AddValue(ColumnId column_id, PackedValueV2 value, ssize_t tail_size) {
  return DoAddValue(column_id, value, tail_size);
}

Result<bool> RowPackerV1::AddValue(
    ColumnId column_id, Slice value_prefix, PackedValueV1 value_suffix, ssize_t tail_size) {
  return DoAddValue(column_id, PackedValueWithPrefixV1(value_prefix, value_suffix), tail_size);
}

Result<bool> RowPackerV1::AddValue(ColumnId column_id, Slice value, ssize_t tail_size) {
  return AddValue(column_id, PackedValueV1(value), tail_size);
}

Result<bool> RowPackerV1::AddValue(
    ColumnId column_id, Slice value_prefix, Slice value_suffix, ssize_t tail_size) {
  return AddValue(column_id, value_prefix, PackedValueV1(value_suffix), tail_size);
}

Result<bool> RowPackerV1::AddValue(ColumnId column_id, const PackableValue& value) {
  return DoAddValue(column_id, value, /* tail_size= */ 0);
}

void RowPackerV1::Restart() {
  // TODO(packed_row) Remove after full support for packed row v2 is merged.
  // Never used, actually.
  LOG(FATAL) << "Should not be invoked";
}

template <class Value>
Result<bool> RowPackerV1::DoAddValue(ColumnId column_id, const Value& value, ssize_t tail_size) {
  return DoAddValueImpl<ColumnPackerV1>(column_id, value, tail_size);
}

Result<Slice> RowPackerV1::Complete() {
  RETURN_NOT_OK(CompleteColumns(idx_, this));
  return result_.AsSlice();
}

void RowPackerV2::Init(SchemaVersion version) {
  // V2 packed row has the following header:
  // kPackedRowV2
  // schema version
  // flags
  // optional null mask
  auto null_mask_size = packing_.NullMaskSize();
  auto* out = result_.GrowByAtLeast(1 + kMaxVarint32Length + 1 + null_mask_size);
  auto* data = pointer_cast<char*>(result_.mutable_data());
  *out++ = ValueEntryTypeAsChar::kPackedRowV2;
  out += FastEncodeUnsignedVarInt(version, out);
  *out++ = 0; // flags
  var_header_start_ = out - data;
  memset(out, 0, null_mask_size);
  out += null_mask_size;
  prefix_end_ = out - data;
  result_.Truncate(prefix_end_);
}

Result<bool> RowPackerV2::AddValue(ColumnId column_id, PackedValueV2 value, ssize_t tail_size) {
  return DoAddValue(column_id, value, tail_size);
}

Result<bool> RowPackerV2::AddValue(ColumnId column_id, PackedValueV1 value, ssize_t tail_size) {
  return DoAddValue(column_id, value, tail_size);
}

Result<bool> RowPackerV2::AddValue(
    ColumnId column_id, Slice value_prefix, PackedValueV1 value_suffix, ssize_t tail_size) {
  return DoAddValue(column_id, std::pair(value_prefix, value_suffix), tail_size);
}

Result<bool> RowPackerV2::AddValue(ColumnId column_id, const QLValuePB& value) {
  return DoAddValue(column_id, value, /* tail_size= */ 0);
}

Result<bool> RowPackerV2::AddValue(ColumnId column_id, const LWQLValuePB& value) {
  return DoAddValue(column_id, value, /* tail_size= */ 0);
}

Result<bool> RowPackerV2::AddValue(ColumnId column_id, const PackableValue& value) {
  return DoAddValue(column_id, value, /* tail_size= */ 0);
}

template <class Value>
Result<bool> RowPackerV2::DoAddValue(ColumnId column_id, const Value& value, ssize_t tail_size) {
  return DoAddValueImpl<ColumnPackerV2>(column_id, value, tail_size);
}

Result<Slice> RowPackerV2::Complete() {
  RETURN_NOT_OK(CompleteColumns(idx_, this));
  auto* data = result_.mutable_data();
  bool has_null = false;
  for (auto x = data + var_header_start_, end = data + prefix_end_; x != end; ++x) {
    if (*x) {
      has_null = true;
      break;
    }
  }
  if (!has_null) {
    auto start = data + prefix_end_ - var_header_start_;
    memmove(start, data, var_header_start_);
    Slice result(start, result_.end());
    VLOG_WITH_FUNC(4) << "no nulls: " << result.ToDebugHexString();
    return result;
  }

  data[var_header_start_ - 1] |= kHasNullsFlag;
  VLOG_WITH_FUNC(4) << "with nulls: " << result_.AsSlice().ToDebugHexString();
  return result_.AsSlice();
}

// Replaces the schema version in packed value with the provided schema version.
// Note: Value starts with the value type (does not contain control fields).
Status ReplaceSchemaVersionInPackedValue(Slice value,
                                         const ValueControlFields& control_fields,
                                         const SchemaVersionMapper& schema_versions_mapper,
                                         ValueBuffer *out) {
  CHECK_NOTNULL(out);
  out->Truncate(0);
  control_fields.AppendEncoded(out);

  auto value_type = value.consume_byte();
  auto schema_version = narrow_cast<SchemaVersion>(VERIFY_RESULT(
      FastDecodeUnsignedVarInt(&value)));
  auto mapped_version = VERIFY_RESULT(schema_versions_mapper(schema_version));

  out->Reserve(out->size() + 1 + kMaxVarint32Length + value.size());
  out->PushBack(value_type);
  FastAppendUnsignedVarInt(mapped_version, out);
  out->Append(value);

  return Status::OK();
}

RowPackerBase& PackerBase(RowPackerVariant* packer_variant) {
  return *std::visit([](auto& packer) {
    RowPackerBase* base = &packer;
    return base;
  }, *packer_variant);
}

} // namespace yb::dockv
