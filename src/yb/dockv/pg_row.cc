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

#include "yb/dockv/pg_row.h"

#include "yb/common/ql_type.h"
#include "yb/common/types.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value_type.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/decimal.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb::dockv {

namespace {

size_t FixedSize(DataType data_type) {
  switch (data_type) {
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::BOOL: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::GIN_NULL:
      return 1;
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::UINT16:
      return 2;
    case DataType::INT32: FALLTHROUGH_INTENDED;
    case DataType::FLOAT: FALLTHROUGH_INTENDED;
    case DataType::UINT32:
      return 4;
    case DataType::INT64: FALLTHROUGH_INTENDED;
    case DataType::DOUBLE: FALLTHROUGH_INTENDED;
    case DataType::TIMESTAMP: FALLTHROUGH_INTENDED;
    case DataType::UINT64:
      return 8;

    case DataType::STRING: FALLTHROUGH_INTENDED;
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::DECIMAL: FALLTHROUGH_INTENDED;
    case DataType::VARINT:
      return 0;

    case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case DataType::INET: FALLTHROUGH_INTENDED;
    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::UUID: FALLTHROUGH_INTENDED;
    case DataType::TIMEUUID: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DataType::FROZEN: FALLTHROUGH_INTENDED;
    case DataType::DATE: FALLTHROUGH_INTENDED;
    case DataType::TIME: FALLTHROUGH_INTENDED;
    case DataType::JSONB:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(DataType, data_type);
}

bool FixedSize(const QLTypePtr& type) {
  return FixedSize(type->main());
}

bool StoreAsValue(DataType data_type) {
  return FixedSize(data_type) != 0;
}

bool StoreAsValue(const QLTypePtr& type) {
  return StoreAsValue(type->main());
}

void AppendString(const Slice& slice, ValueBuffer* buffer, bool append_zero) {
  int64_t length = slice.size();
  char* out = buffer->GrowByAtLeast(sizeof(uint64_t) + length + append_zero);
  BigEndian::Store64(out, length + append_zero);
  out += sizeof(uint64_t);
  memcpy(out, slice.cdata(), length);
  if (append_zero) {
    out[length] = 0;
  }
}

Status DoDecodeValue(
    const Slice& rocksdb_slice, const QLTypePtr& ql_type,
    bool* is_null, PgValueDatum* value, ValueBuffer* buffer) {
  RSTATUS_DCHECK(!rocksdb_slice.empty(), Corruption, "Cannot decode a value from an empty slice");
  Slice slice(rocksdb_slice);

  const auto value_type = static_cast<ValueEntryType>(slice.consume_byte());
  if (value_type == ValueEntryType::kNullHigh ||
      value_type == ValueEntryType::kNullLow ||
      value_type == ValueEntryType::kTombstone) {
    *is_null = true;
    return Status::OK();
  }

  *is_null = false;

  switch (value_type) {
    case ValueEntryType::kFalse: FALLTHROUGH_INTENDED;
    case ValueEntryType::kTrue:
      if (ql_type->main() != DataType::BOOL) {
        return STATUS_FORMAT(
            Corruption, "Wrong datatype $0 for boolean value type $1",
            DataType_Name(ql_type->main()), value_type);
      }
      *value = value_type != ValueEntryType::kFalse;
      return Status::OK();

    case ValueEntryType::kInt32: FALLTHROUGH_INTENDED;
    case ValueEntryType::kWriteId: FALLTHROUGH_INTENDED;
    case ValueEntryType::kFloat: {
      RSTATUS_DCHECK_EQ(
          slice.size(), sizeof(int32_t), Corruption,
          Format("Invalid number of bytes for a $0", value_type));
      *value = BigEndian::Load32(slice.data());
      return Status::OK();
    }

    case ValueEntryType::kUInt32: {
      RSTATUS_DCHECK_EQ(
          slice.size(), sizeof(uint32_t), Corruption,
          Format("Invalid number of bytes for a $0", value_type));
      *value = BigEndian::Load32(slice.data());
      return Status::OK();
    }
    case ValueEntryType::kInt64: FALLTHROUGH_INTENDED;
    case ValueEntryType::kArrayIndex: FALLTHROUGH_INTENDED;
    case ValueEntryType::kDouble: {
      RSTATUS_DCHECK_EQ(
          slice.size(), sizeof(int64_t), Corruption,
          Format("Invalid number of bytes for a $0", value_type));
      *value = BigEndian::Load64(slice.data());
      return Status::OK();
    }

    case ValueEntryType::kCollString: FALLTHROUGH_INTENDED;
    case ValueEntryType::kDecimal: FALLTHROUGH_INTENDED;
    case ValueEntryType::kString: {
      *value = buffer->size();
      AppendString(slice, buffer, ql_type->main() != DataType::BINARY);
      return Status::OK();
    }
    default:
      break;
  }

  RSTATUS_DCHECK(
      false, Corruption, "Wrong value type $0 in $1 OR unsupported datatype $2",
      value_type, rocksdb_slice.ToDebugHexString(), DataType_Name(ql_type->main()));
}

Result<const char*> ExtractPrefix(Slice* slice, size_t required, const char* name) {
  RSTATUS_DCHECK_GE(
      slice->size(), required, Corruption,
      Format("Not enough bytes to decode a $0", name));
  auto result = slice->cdata();
  slice->remove_prefix(required);
  return result;
}

Status DoDecodeKey(
    Slice* slice, const QLTypePtr& ql_type,
    bool* is_null, PgValueDatum* value, ValueBuffer* buffer) {
  // A copy for error reporting.
  const auto input_slice = *slice;

  RSTATUS_DCHECK(!slice->empty(), Corruption, "Cannot decode the key entry from empty slice");
  auto type = static_cast<KeyEntryType>(slice->consume_byte());
  if (type == KeyEntryType::kNullLow || type == KeyEntryType::kNullHigh) {
    *is_null = true;
    return Status::OK();
  }

  *is_null = false;

  switch (type) {
    case KeyEntryType::kFalse: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFalseDescending:
      *value = 0;
      return Status::OK();
    case KeyEntryType::kTrue: FALLTHROUGH_INTENDED;
    case KeyEntryType::kTrueDescending:
      *value = 1;
      return Status::OK();

    case KeyEntryType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kStringDescending: {
      *value = buffer->size();
      std::string result;
      RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &result)); // TODO GH #17267
      AppendString(result, buffer, ql_type->main() != DataType::BINARY);
      return Status::OK();
    }

    case KeyEntryType::kCollString: FALLTHROUGH_INTENDED;
    case KeyEntryType::kString: {
      *value = buffer->size();
      std::string result;
      RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &result)); // TODO GH #17267
      AppendString(result, buffer, ql_type->main() != DataType::BINARY);
      return Status::OK();
    }
    case KeyEntryType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDecimal: {
      *value = buffer->size();
      util::Decimal decimal;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(*slice, &num_decoded_bytes));
      slice->remove_prefix(num_decoded_bytes);

      if (type == KeyEntryType::kDecimalDescending) {
        // When we encode a descending decimal, we do a bitwise negation of each byte, which changes
        // the sign of the number. This way we reverse the sorting order. decimal.Negate() restores
        // the original sign of the number.
        decimal.Negate();
      }
      AppendString(decimal.EncodeToComparable(), buffer, true);
      return Status::OK();
    }

    case KeyEntryType::kInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt32: {
      *value = make_unsigned(util::DecodeInt32FromKey(
          VERIFY_RESULT(ExtractPrefix(slice, sizeof(int32_t), "32-bit integer"))));
      if (type != KeyEntryType::kInt32) {
        *value = ~*value;
      }
      return Status::OK();
    }

    case KeyEntryType::kColocationId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case KeyEntryType::kUInt32:
      *value = BigEndian::Load32(VERIFY_RESULT(ExtractPrefix(
          slice, sizeof(uint32_t), "32-bit unsigned integer")));
      return Status::OK();

    case KeyEntryType::kUInt64Descending: {
      *value = ~BigEndian::Load64(VERIFY_RESULT(ExtractPrefix(
          slice, sizeof(uint64_t), "64-bit unsigned integer")));
      return Status::OK();
    }
    case KeyEntryType::kUInt64:
      *value = BigEndian::Load64(VERIFY_RESULT(ExtractPrefix(
          slice, sizeof(uint64_t), "64-bit unsigned integer")));
      return Status::OK();

    case KeyEntryType::kInt64Descending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kInt64: {
      *value = util::DecodeInt64FromKey(
          VERIFY_RESULT(ExtractPrefix(slice, sizeof(int64_t), "64-bit integer")));
      if (type == KeyEntryType::kInt64Descending) {
        *value = ~*value;
      }
      return Status::OK();
    }

    case KeyEntryType::kFloatDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kFloat: {
      *value = bit_cast<uint32_t>(util::DecodeFloatFromKey(
          VERIFY_RESULT(ExtractPrefix(slice, sizeof(float), "float")),
          type == KeyEntryType::kFloatDescending));
      return Status::OK();
    }

    case KeyEntryType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case KeyEntryType::kDouble: {
      *value = bit_cast<uint64_t>(util::DecodeDoubleFromKey(
          VERIFY_RESULT(ExtractPrefix(slice, sizeof(double), "double")),
          type == KeyEntryType::kDoubleDescending));
      return Status::OK();
    }

    default:
      break;
  }

  RSTATUS_DCHECK(
      false, Corruption,
      "Cannot decode value type $0 from the key encoding format: $1",
      type, input_slice.ToDebugString());
}

} // namespace

int8_t PgValue::int8_value() const {
  return static_cast<int8_t>(value_);
}

int16_t PgValue::int16_value() const {
  return static_cast<int16_t>(value_);
}

int32_t PgValue::int32_value() const {
  return static_cast<int32_t>(value_);
}

uint32_t PgValue::uint32_value() const {
  return static_cast<uint32_t>(value_);
}

int64_t PgValue::int64_value() const {
  return static_cast<int64_t>(value_);
}

uint64_t PgValue::uint64_value() const {
  return static_cast<uint64_t>(value_);
}

float PgValue::float_value() const {
  return bit_cast<float>(uint32_value());
}

double PgValue::double_value() const {
  return bit_cast<double>(uint64_value());
}

bool PgValue::bool_value() const {
  return value_ != 0;
}

Slice PgValue::binary_value() const {
  return vardata();
}

Slice PgValue::string_value() const {
  return vardata().WithoutSuffix(1);
}

Slice PgValue::vardata() const {
  auto data = bit_cast<uint8_t*>(value_);
  auto len = BigEndian::Load64(data);
  return Slice(data + 8, len);
}

QLValuePB PgValue::ToQLValuePB(DataType data_type) const {
  QLValuePB result;
  switch (data_type) {
    case DataType::BINARY: {
      auto data = binary_value();
      result.set_binary_value(data.cdata(), data.size());
      return result;
    }
    case DataType::BOOL:
      result.set_bool_value(bool_value());
      return result;
    case DataType::FLOAT:
      result.set_float_value(float_value());
      return result;
    case DataType::INT8:
      result.set_int8_value(int8_value());
      return result;
    case DataType::INT16:
      result.set_int16_value(int16_value());
      return result;
    case DataType::INT32:
      result.set_int32_value(int32_value());
      return result;
    case DataType::INT64:
      result.set_int64_value(int64_value());
      return result;
    case DataType::DECIMAL: FALLTHROUGH_INTENDED;
    case DataType::STRING: {
      auto data = string_value();
      result.set_string_value(data.cdata(), data.size());
      return result;
    }
    case DataType::UINT32:
      result.set_uint32_value(uint32_value());
      return result;
    case DataType::UINT64:
      result.set_uint64_value(uint64_value());
      return result;
    case DataType::DOUBLE:
      result.set_double_value(double_value());
      return result;
    default:
      break;
  }
  LOG(FATAL) << "Not supported type: " << data_type;
}

void PgValue::AppendTo(DataType data_type, WriteBuffer* out) const {
  const auto fixed_size = FixedSize(data_type);
  if (fixed_size) {
    auto big_endian_value = BigEndian::FromHost64(value_);
    Slice slice(pointer_cast<const uint8_t*>(&big_endian_value), 8);
    out->AppendWithPrefix(0, slice.Suffix(fixed_size));
  }
}

PgTableRow::PgTableRow(std::reference_wrapper<const ReaderProjection> projection)
    : projection_(&projection.get()), is_null_(projection_->size()), values_(projection_->size()) {
}

bool PgTableRow::IsEmpty() const {
  return buffer_.empty();
}

std::string PgTableRow::ToString() const {
  std::string result = "{ ";
  for (size_t i = 0; i != values_.size(); ++i) {
    result += projection_->columns[i].id.ToString();
    result += ": ";
    if (is_null_[i]) {
      result += "<NULL>";
    } else if (StoreAsValue(projection_->columns[i].type)) {
      result += values_[i];
    } else {
      auto data = buffer_.data() + values_[i];
      auto len = BigEndian::Load64(data);
      data += 8;
      result += Slice(data, len).ToDebugHexString();
    }
    result += " ";
  }
  result += "}";
  return result;
}

PgValueDatum PgTableRow::GetDatum(size_t idx) const {
  if (StoreAsValue(projection_->columns[idx].type)) {
    return values_[idx];
  }
  return bit_cast<PgValueDatum>(buffer_.data() + values_[idx]);
}

std::optional<PgValue> PgTableRow::GetValueByIndex(size_t index) const {
  if (is_null_[index]) {
    return std::nullopt;
  }
  return PgValue(GetDatum(index));
}

std::optional<PgValue> PgTableRow::GetValueByColumnId(ColumnIdRep column_id) const {
  auto idx = projection_->ColumnIdxById(ColumnId(column_id));
  if (idx == ReaderProjection::kNotFoundIndex) {
    return std::nullopt;
  }
  return GetValueByIndex(idx);
}

QLValuePB PgTableRow::GetQLValuePB(ColumnIdRep column_id) const {
  size_t idx = projection_->ColumnIdxById(ColumnId(column_id));
  auto value = GetValueByIndex(idx);
  if (!value) {
    return QLValuePB();
  }
  return value->ToQLValuePB(projection_->columns[idx].type->main());
}

void PgTableRow::Clear() {
  buffer_.clear();
}

void PgTableRow::Clear(size_t column_idx) {
  is_null_[column_idx] = true;
}

Status PgTableRow::DecodeValue(size_t column_idx, const Slice& value) {
  return DoDecodeValue(
      value, projection_->columns[column_idx].type,
      &is_null_[column_idx], &values_[column_idx], &buffer_);
}

Status PgTableRow::DecodeKey(size_t column_idx, Slice* value) {
  return DoDecodeKey(
      value, projection_->columns[column_idx].type,
      &is_null_[column_idx], &values_[column_idx], &buffer_);
}

Status PgTableRow::SetValue(ColumnId column_id, const QLValuePB& value) {
  const size_t idx = projection_->ColumnIdxById(column_id);
  if (yb::IsNull(value)) {
    is_null_[idx] = true;
    return Status::OK();
  }
  is_null_[idx] = false;
  const size_t old_size = buffer_.size();
  RETURN_NOT_OK(pggate::WriteColumn(value, &buffer_));
  const auto fixed_size = FixedSize(projection_->columns[idx].type);
  if (fixed_size != 0) {
    values_[idx] = BigEndian::Load64VariableLength(buffer_.data() + old_size, fixed_size);
    buffer_.Truncate(old_size);
  } else {
    values_[idx] = old_size;
  }
  return Status::OK();
}

}  // namespace yb::dockv
