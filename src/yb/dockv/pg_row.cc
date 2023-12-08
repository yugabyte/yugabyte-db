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

#include "yb/common/constants.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/types.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/value_packing.h"
#include "yb/dockv/value_type.h"

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

bool StoreAsValue(DataType data_type) {
  return FixedSize(data_type) != 0;
}

// Return appended string offset in the buffer.
size_t AppendString(Slice slice, ValueBuffer* buffer, bool append_zero) {
  auto result = buffer->size();
  int64_t length = slice.size();
  char* out = buffer->GrowByAtLeast(sizeof(uint64_t) + length + append_zero);
  BigEndian::Store64(out, length + append_zero);
  out += sizeof(uint64_t);
  memcpy(out, slice.cdata(), length);
  if (append_zero) {
    out[length] = 0;
  }
  return result;
}

bool IsNull(char value_type) {
  return value_type == ValueEntryTypeAsChar::kNullLow ||
         // Need to check tombstone case since we could have data from old releases that don't
         // convert tombstone records to null during compaction.
         value_type == ValueEntryTypeAsChar::kTombstone;
}

struct VisitDoDecodeValueV2 {
  PackedValueV2 input;
  PgValueDatum* value;
  ValueBuffer* buffer;

  Status Binary() const {
    *value = AppendString(*input, buffer, false);
    return Status::OK();
  }

  Status Decimal() const {
    return String();
  }

  Status String() const {
    *value = AppendString(*input, buffer, true);
    return Status::OK();
  }

  template <class T>
  Status Primitive() const {
#ifdef IS_LITTLE_ENDIAN
    *value = 0;
    memcpy(value, input->data(), sizeof(T));
    return Status::OK();
#else
    #error "Big endian not implemented"
#endif
  }
};

Status DoDecodeValueV2(
    PackedValueV2 input, DataType data_type,
    bool* is_null, PgValueDatum* value, ValueBuffer* buffer) {
  if (input.IsNull()) {
    *is_null = true;
    return Status::OK();
  }
  *is_null = false;
  VisitDoDecodeValueV2 visitor {
    .input = input,
    .value = value,
    .buffer = buffer,
  };
  return VisitDataType(data_type, visitor);
}

Status DoDecodeValue(
    Slice slice, DataType data_type, bool* is_null, PgValueDatum* value, ValueBuffer* buffer) {
  RSTATUS_DCHECK(!slice.empty(), Corruption, "Cannot decode a value from an empty slice");
  auto original_start = slice.data();

  const auto value_type_char = slice.consume_byte();
  if (IsNull(value_type_char)) {
    *is_null = true;
    return Status::OK();
  }

  const auto value_type = static_cast<ValueEntryType>(value_type_char);
  *is_null = false;

  switch (data_type) {
    case DataType::BOOL:
      if (value_type == ValueEntryType::kTrue) {
        *value = 1;
        return Status::OK();
      }
      if (value_type == ValueEntryType::kFalse) {
        *value = 0;
        return Status::OK();
      }
      break;
    case DataType::INT8: [[fallthrough]];
    case DataType::INT16: [[fallthrough]];
    case DataType::INT32: [[fallthrough]];
    case DataType::FLOAT: [[fallthrough]];
    case DataType::UINT8: [[fallthrough]];
    case DataType::UINT16: [[fallthrough]];
    case DataType::UINT32:
      RSTATUS_DCHECK_EQ(
          slice.size(), sizeof(int32_t), Corruption,
          Format("Invalid number of bytes for a $0", data_type));
      *value = BigEndian::Load32(slice.data());
      return Status::OK();
    case DataType::DOUBLE: [[fallthrough]];
    case DataType::INT64: [[fallthrough]];
    case DataType::UINT64:
      RSTATUS_DCHECK_EQ(
          slice.size(), sizeof(int64_t), Corruption,
          Format("Invalid number of bytes for a $0", data_type));
      *value = BigEndian::Load64(slice.data());
      return Status::OK();
    case DataType::DECIMAL: [[fallthrough]];
    case DataType::STRING:
      *value = AppendString(slice, buffer, true);
      return Status::OK();
    case DataType::BINARY:
      *value = AppendString(slice, buffer, false);
      return Status::OK();
    default:
      break;
  }

  RSTATUS_DCHECK(
      false, Corruption, "Wrong value type $0 in $1 OR unsupported datatype $2",
      value_type, Slice(original_start, slice.end()).ToDebugHexString(), data_type);
}

template <class T, bool kLast>
void EncodePrimitive(const PgTableRow& row, WriteBuffer* buffer, const PgWireEncoderEntry* chain) {
  auto index = chain->data;
  if (PREDICT_FALSE(row.IsNull(index))) {
    buffer->PushBack(1);
    CallNextEncoder<kLast>(row, buffer, chain);
    return;
  }

  auto datum = row.GetPrimitiveDatum(index);
  auto value = LoadRaw<T, BigEndian>(&datum);
  buffer->AppendWithPrefix(0, pointer_cast<const char*>(&value), sizeof(value));
  CallNextEncoder<kLast>(row, buffer, chain);
}

template <bool kLast>
void EncodeBinary(const PgTableRow& row, WriteBuffer* buffer, const PgWireEncoderEntry* chain) {
  auto index = chain->data;
  if (PREDICT_FALSE(row.IsNull(index))) {
    buffer->PushBack(1);
    CallNextEncoder<kLast>(row, buffer, chain);
    return;
  }

  auto slice = row.GetVarlenSlice(index);
  buffer->AppendWithPrefix(0, slice);
  CallNextEncoder<kLast>(row, buffer, chain);
}

template <bool kLast>
struct EncoderProvider {
  template <class T>
  PgWireEncoder Primitive() const {
    return EncodePrimitive<T, kLast>;
  }

  PgWireEncoder Binary() const {
    return EncodeBinary<kLast>;
  }

  PgWireEncoder String() const {
    return Binary();
  }

  PgWireEncoder Decimal() const {
    return Binary();
  }
};

template <bool kLast>
struct DecodeBoolColumn {
  UnsafeStatus V1(
      const PackedColumnDecoderData& data, size_t projection_index,
      const PackedColumnDecoderEntry* chain, ValueEntryType value_type, Slice column_value) {
    auto* row = static_cast<PgTableRow*>(data.context);
    if (!column_value.empty()) {
      return STATUS_FORMAT(
          Corruption, "Non empty value for BOOL column", column_value.ToDebugHexString())
          .UnsafeRelease();
    }
    if (value_type == ValueEntryType::kTrue) {
      row->SetDatum(projection_index, 1);
    } else if (value_type == ValueEntryType::kFalse) {
      row->SetDatum(projection_index, 0);
    } else {
      return STATUS_FORMAT(
          Corruption, "Unexpected value type for BOOL column: $0", value_type).UnsafeRelease();
    }

    return CallNextDecoder<kLast>(data, projection_index, chain);
  }
};

template <bool kLast, class Decoder>
struct DecodePgTableRow {
  UnsafeStatus operator()(
      const PackedColumnDecoderData& data, size_t projection_index,
      const PackedColumnDecoderEntry* chain, Slice column_value) {
    auto row = static_cast<PgTableRow*>(data.context);
    auto value_type_char = column_value.consume_byte();
    if (PREDICT_FALSE(IsNull(value_type_char))) {
      row->SetNull(projection_index);
      return CallNextDecoder<kLast>(data, projection_index, chain);
    }
    Decoder decoder;
    return decoder(
        data, projection_index, chain, static_cast<ValueEntryType>(value_type_char), column_value);
  }
};

Result<const char*> StripHybridTime(const char* begin, const char* end) {
  if (*begin == dockv::KeyEntryTypeAsChar::kHybridTime) {
    ++begin;
    return DocHybridTime::EncodedFromStart(begin, end);
  }

  auto status = STATUS_FORMAT(
      Corruption, "Unexpected value type: $0", static_cast<ValueEntryType>(*begin));
  RSTATUS_DCHECK_OK(status);
  return status;
}

template <class T> struct GetValueType;

template <>
struct GetValueType<float> {
  static constexpr ValueEntryType kValue = ValueEntryType::kFloat;
};

template <>
struct GetValueType<int8_t> {
  static constexpr ValueEntryType kValue = ValueEntryType::kInt32;
};

template <>
struct GetValueType<int16_t> {
  static constexpr ValueEntryType kValue = ValueEntryType::kInt32;
};

template <>
struct GetValueType<int32_t> {
  static constexpr ValueEntryType kValue = ValueEntryType::kInt32;
};

template <>
struct GetValueType<uint32_t> {
  static constexpr ValueEntryType kValue = ValueEntryType::kUInt32;
};

template <>
struct GetValueType<double> {
  static constexpr ValueEntryType kValue = ValueEntryType::kDouble;
};

template <>
struct GetValueType<int64_t> {
  static constexpr ValueEntryType kValue = ValueEntryType::kInt64;
};

template <>
struct GetValueType<uint64_t> {
  static constexpr ValueEntryType kValue = ValueEntryType::kUInt64;
};

template <class T>
struct PrimitiveValueDecoder {
  bool V1(PgTableRow* row, size_t projection_index, const char* begin, const char* end) const {
    auto value_type = GetValueType<T>::kValue;
    constexpr size_t kEncodedSize = sizeof(T) <= 4 ? 4 : 8;
    if (PREDICT_FALSE(end - begin != 1 + kEncodedSize)) {
      return false;
    }
    auto value_type_char = *begin;
    if (PREDICT_FALSE(value_type_char != static_cast<char>(value_type))) {
      return false;
    }
    ++begin;
    if (kEncodedSize == 4) {
      row->SetDatum(projection_index, BigEndian::Load32(begin));
    } else {
      row->SetDatum(projection_index, BigEndian::Load64(begin));
    }
    return true;
  }

  void V2(PgTableRow* row, size_t projection_index, const char* begin, const char* end) const {
#ifdef IS_LITTLE_ENDIAN
    PgValueDatum value = 0;
    memcpy(&value, begin, sizeof(T));
    row->SetDatum(projection_index, value);
#else
    #error "Big endian not implemented"
#endif
  }
};

template <>
struct PrimitiveValueDecoder<bool> {
  bool V1(PgTableRow* row, size_t projection_index, const char* begin, const char* end) const {
    if (PREDICT_FALSE(end - begin != 1)) {
      return false;
    }
    auto value_type_char = *begin;
    if (value_type_char == ValueEntryTypeAsChar::kTrue) {
      row->SetDatum(projection_index, 1);
    } else if (value_type_char == ValueEntryTypeAsChar::kFalse) {
      row->SetDatum(projection_index, 0);
    } else {
      return false;
    }
    return true;
  }

  void V2(PgTableRow* row, size_t projection_index, const char* begin, const char* end) const {
    row->SetDatum(projection_index, *begin);
  }
};

template <bool kAppendZero, char kValueType>
struct BinaryValueDecoder {
  bool V1(PgTableRow* row, size_t projection_index, const char* begin, const char* end) const {
    if (PREDICT_FALSE(begin == end)) {
      return false;
    }
    if (PREDICT_FALSE(*begin != kValueType)) {
      return false;
    }
    row->SetBinary(projection_index, Slice(++begin, end), kAppendZero);
    return true;
  }

  void V2(PgTableRow* row, size_t projection_index, const char* begin, const char* end) const {
    row->SetBinary(projection_index, Slice(begin, end), kAppendZero);
  }
};

template <bool kLast, class Decoder>
UnsafeStatus DoDecodePackedColumn(
    const PackedColumnDecoderData& data, size_t projection_index,
    const PackedColumnDecoderEntry* chain, const char* begin, const char* end) {
  auto* row = static_cast<PgTableRow*>(data.context);
  Decoder decoder;
  if (decoder.V1(row, projection_index, begin, end)) {
    return CallNextDecoder<kLast>(data, projection_index, chain);
  }

  if (PREDICT_FALSE(begin == end)) {
    row->SetNull(projection_index);
    return CallNextDecoder<kLast>(data, projection_index, chain);
  }

  auto value_type_char = *begin;
  if (IsNull(value_type_char)) {
    row->SetNull(projection_index);
    return CallNextDecoder<kLast>(data, projection_index, chain);
  }

  auto result = StripHybridTime(begin, end);
  if (!result.ok()) {
    return result.status().UnsafeRelease();
  }
  return DoDecodePackedColumn<kLast, Decoder>(data, projection_index, chain, *result, end);
}

template <bool kLast, class Decoder>
UnsafeStatus DoDecodePackedColumn(
    const PackedColumnDecoderData& data, size_t projection_index,
    const PackedColumnDecoderEntry* chain, PackedValueV1 value) {
  return DoDecodePackedColumn<kLast, Decoder>(
      data, projection_index, chain, value->cdata(), value->cend());
}

template <bool kLast, class Decoder>
UnsafeStatus DoDecodePackedColumn(
    const PackedColumnDecoderData& data, size_t projection_index,
    const PackedColumnDecoderEntry* chain, PackedValueV2 value) {
  auto* row = static_cast<PgTableRow*>(data.context);
  if (PREDICT_FALSE(value.IsNull())) {
    row->SetNull(projection_index);
    return CallNextDecoder<kLast>(data, projection_index, chain);
  }
  Decoder decoder;
  decoder.V2(row, projection_index, value->cdata(), value->cend());
  return CallNextDecoder<kLast>(data, projection_index, chain);
}

template <class RowDecoder, bool kLast, class T>
UnsafeStatus DecodePackedColumn(
    const PackedColumnDecoderData& data, size_t projection_index,
    const PackedColumnDecoderEntry* chain) {
  auto column_value = static_cast<RowDecoder*>(data.decoder)->FetchValue(chain->data);
  return DoDecodePackedColumn<kLast, T>(
      data, projection_index, chain, column_value);
}

template <class RowDecoder, bool kLast>
struct GetPackedColumnDecoderVisitor {
  template <class T>
  PackedColumnDecoder Primitive() const {
    return DecodePackedColumn<RowDecoder, kLast, PrimitiveValueDecoder<T>>;
  }

  PackedColumnDecoder Binary() const {
    return DecodePackedColumn<
        RowDecoder, kLast, BinaryValueDecoder<false, ValueEntryTypeAsChar::kString>>;
  }

  PackedColumnDecoder String() const {
    return DecodePackedColumn<
        RowDecoder, kLast, BinaryValueDecoder<true, ValueEntryTypeAsChar::kString>>;
  }

  PackedColumnDecoder Decimal() const {
    return DecodePackedColumn<
        RowDecoder, kLast, BinaryValueDecoder<true, ValueEntryTypeAsChar::kDecimal>>;
  }
};

template <class RowDecoder>
PackedColumnDecoder GetPackedColumnDecoder2(bool last, DataType data_type) {
  if (last) {
    return VisitDataType(data_type, GetPackedColumnDecoderVisitor<RowDecoder, true>());
  } else {
    return VisitDataType(data_type, GetPackedColumnDecoderVisitor<RowDecoder, false>());
  }
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
  return Vardata();
}

Slice PgValue::string_value() const {
  return Vardata().WithoutSuffix(1);
}

Slice PgValue::Vardata() const {
  auto data = bit_cast<uint8_t*>(value_);
  auto len = BigEndian::Load64(data);
  return Slice(data + 8, len);
}

Slice PgValue::VardataWithLen() const {
  const auto data = bit_cast<uint8_t*>(value_);
  const auto len = BigEndian::Load64(data);
  return Slice(data, len + 8);
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

template <class Buffer>
void PgValue::DoAppendTo(DataType data_type, Buffer* out) const {
  const auto fixed_size = FixedSize(data_type);
  if (fixed_size) {
    auto big_endian_value = BigEndian::FromHost64(value_);
    Slice slice(pointer_cast<const uint8_t*>(&big_endian_value), 8);
    out->AppendWithPrefix(0, slice.Suffix(fixed_size));
  } else {
    out->AppendWithPrefix(0, VardataWithLen());
  }
}

void PgValue::AppendTo(DataType data_type, WriteBuffer* out) const {
  DoAppendTo(data_type, out);
}

void PgValue::AppendTo(DataType data_type, ValueBuffer* out) const {
  DoAppendTo(data_type, out);
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
    } else if (StoreAsValue(projection_->columns[i].data_type)) {
      result += std::to_string(values_[i]);
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
  if (StoreAsValue(projection_->columns[idx].data_type)) {
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

PgWireEncoderEntry PgTableRow::GetEncoder(size_t index, bool last) const {
  auto data_type = projection_->columns[index].data_type;
  return PgWireEncoderEntry {
    .encoder = !last ? VisitDataType(data_type, EncoderProvider<false>())
                     : VisitDataType(data_type, EncoderProvider<true>()),
    .data = index,
  };
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
  return value->ToQLValuePB(projection_->columns[idx].data_type);
}

void PgTableRow::Reset() {
  buffer_.clear();
}

Status PgTableRow::SetNullOrMissingResult(const Schema& schema) {
  for (size_t i = 0; i != is_null_.size(); ++i) {
    const auto& column_schema =
        VERIFY_RESULT_REF(schema.column_by_id(projection_->columns[i].id));
    const auto& missing_value = column_schema.missing_value();
    RETURN_NOT_OK(SetValueByColumnIdx(i, missing_value));
  }
  return Status::OK();
}

void PgTableRow::SetNull(size_t column_idx) {
  is_null_[column_idx] = true;
}

Status PgTableRow::DecodeValue(size_t column_idx, PackedValueV1 value) {
  DCHECK_LT(column_idx, projection_->columns.size());
  return DoDecodeValue(
      *value, projection_->columns[column_idx].data_type,
      &is_null_[column_idx], &values_[column_idx], &buffer_);
}

Status PgTableRow::DecodeValue(size_t column_idx, PackedValueV2 value) {
  DCHECK_LT(column_idx, projection_->columns.size());
  return DoDecodeValueV2(
      value, projection_->columns[column_idx].data_type,
      &is_null_[column_idx], &values_[column_idx], &buffer_);
}

PgValue PgTableRow::TrimString(size_t idx, size_t skip_prefix, size_t new_len) {
  DCHECK_EQ(projection_->columns[idx].data_type, DataType::STRING);
  auto& value = values_[idx];
  value += skip_prefix;
  auto* start = buffer_.mutable_data() + value;
  BigEndian::Store64(start, new_len + 1);
  return PgValue(bit_cast<PgValueDatum>(start));
}

Status PgTableRow::SetValue(ColumnId column_id, const QLValuePB& value) {
  const size_t idx = projection_->ColumnIdxById(column_id);
  return SetValueByColumnIdx(idx, value);
}

Status PgTableRow::SetValueByColumnIdx(size_t idx, const QLValuePB& value) {
  if (yb::IsNull(value)) {
    is_null_[idx] = true;
    return Status::OK();
  }
  is_null_[idx] = false;
  const size_t old_size = buffer_.size();
  RETURN_NOT_OK(pggate::WriteColumn(value, &buffer_));
  const auto fixed_size = FixedSize(projection_->columns[idx].data_type);
  if (fixed_size != 0) {
    values_[idx] = BigEndian::Load64VariableLength(
        buffer_.data() + old_size + pggate::PgWireDataHeader::kSerializedSize, fixed_size);
    buffer_.Truncate(old_size);
  } else {
    values_[idx] = old_size + pggate::PgWireDataHeader::kSerializedSize;
  }
  return Status::OK();
}

Result<const char*> PgTableRow::DecodeComparableString(
    size_t column_idx, const char* input, const char* end, bool append_zero,
    SortOrder sort_order) {
  auto old_size = buffer_.size();
  buffer_.GrowByAtLeast(sizeof(uint64_t));
  const auto* result = VERIFY_RESULT(sort_order == SortOrder::kAscending
      ? DecodeZeroEncodedStr(input, end, &buffer_)
      : DecodeComplementZeroEncodedStr(input, end, &buffer_));
  if (append_zero) {
    buffer_.PushBack(0);
  }
  BigEndian::Store64(
      buffer_.mutable_data() + old_size, buffer_.size() - old_size - sizeof(uint64_t));
  is_null_[column_idx] = false;
  values_[column_idx] = old_size;
  return result;
}

void PgTableRow::SetBinary(size_t column_idx, Slice value, bool append_zero) {
  is_null_[column_idx] = false;
  values_[column_idx] = buffer_.size();
  AppendString(value, &buffer_, append_zero);
}

PackedColumnDecoder PgTableRow::GetPackedColumnDecoder(
    PackedRowVersion version, bool last, DataType data_type) {
  switch (version) {
    case PackedRowVersion::kV1:
      return GetPackedColumnDecoder2<PackedRowDecoderV1>(last, data_type);
    case PackedRowVersion::kV2:
      return GetPackedColumnDecoder2<PackedRowDecoderV2>(last, data_type);
  }
  FATAL_INVALID_ENUM_VALUE(PackedRowVersion, version);
}

}  // namespace yb::dockv
