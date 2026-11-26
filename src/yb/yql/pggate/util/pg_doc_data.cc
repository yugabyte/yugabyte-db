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

#include "yb/yql/pggate/util/pg_doc_data.h"

#include "yb/common/ql_value.h"
#include "yb/common/value.messages.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

#include "yb/util/status_format.h"

namespace yb {
namespace pggate {

namespace {

template <class Value, class Buffer> requires (std::is_integral<Value>::value)
void PgWriteInt(Value value, Buffer* buffer) {
  char buf[PgWireDataHeader::kSerializedSize + sizeof(value)];
  PgWireDataHeader().SerializeTo(buf);
  Store<Value, NetworkByteOrder>(buf + PgWireDataHeader::kSerializedSize, value);
  buffer->Append(buf, sizeof(buf));
}

template <class Int, class Value, class Buffer>
    requires (std::is_integral<Int>::value && std::is_floating_point<Value>::value)
void PgWriteFloat(Value value, Buffer* buffer) {
  PgWriteInt(bit_cast<Int>(value), buffer);
}

template <class Buffer>
void PgWriteNull(Buffer* buffer) {
  PgWireDataHeader header;
  header.set_null();
  char buf[PgWireDataHeader::kSerializedSize];
  header.SerializeTo(buf);
  buffer->Append(buf, sizeof(buf));
}

template <bool kNullTerminated, class Buffer>
void PgWriteBytes(const Slice& value, Buffer* buffer) {
  auto length = value.size() + kNullTerminated;
  PgWriteInt<uint64_t>(length, buffer);
  buffer->Append(value);
  if constexpr (kNullTerminated) {
    buffer->PushBack(0);
  }
}

template <class Value, class Buffer>
Status DoWriteColumn(const Value& col_value, Buffer* buffer) {
  // Write data header.
  if (IsNull(col_value)) {
    PgWriteNull(buffer);
    return Status::OK();
  }

  switch (col_value.value_case()) {
    case InternalType::kBoolValue:
      PgWriteInt<uint8_t>(col_value.bool_value(), buffer);
      break;
    case InternalType::kInt8Value:
      PgWriteInt<int8_t>(col_value.int8_value(), buffer);
      break;
    case InternalType::kInt16Value:
      PgWriteInt<int16_t>(col_value.int16_value(), buffer);
      break;
    case InternalType::kInt32Value:
      PgWriteInt<int32_t>(col_value.int32_value(), buffer);
      break;
    case InternalType::kInt64Value:
      PgWriteInt<int64_t>(col_value.int64_value(), buffer);
      break;
    case InternalType::kUint32Value:
      PgWriteInt<uint32_t>(col_value.uint32_value(), buffer);
      break;
    case InternalType::kUint64Value:
      PgWriteInt<uint64_t>(col_value.uint64_value(), buffer);
      break;
    case InternalType::kFloatValue:
      PgWriteFloat<uint32_t>(col_value.float_value(), buffer);
      break;
    case InternalType::kDoubleValue:
      PgWriteFloat<uint64_t>(col_value.double_value(), buffer);
      break;
    case InternalType::kStringValue:
      PgWriteBytes</* null_terminating= */ true>(col_value.string_value(), buffer);
      break;
    case InternalType::kBinaryValue:
      PgWriteBytes</* null_terminating= */ false>(col_value.binary_value(), buffer);
      break;
    case InternalType::kDecimalValue:
      // Passing a serialized form of YB Decimal, decoding will be done in pg_expr.cc
      PgWriteBytes</* null_terminating= */ true>(col_value.decimal_value(), buffer);
      break;
    case InternalType::kBsonValue:
      PgWriteBytes</* null_terminating= */ false>(col_value.bson_value(), buffer);
      break;
    case InternalType::kVirtualValue:
      // Expecting database to return an actual value and not a virtual one.
    case InternalType::kTimestampValue:
    case InternalType::kDateValue: // Not used for PG storage
    case InternalType::kTimeValue: // Not used for PG storage
    case InternalType::kVarintValue:
    case InternalType::kInetaddressValue:
    case InternalType::kJsonbValue:
    case InternalType::kUuidValue:
    case InternalType::kTimeuuidValue:
      // PgGate has not supported these datatypes yet.
      return STATUS_FORMAT(NotSupported,
          "Unexpected data was read from database: col_value.type()=$0", col_value.value_case());

    case InternalType::VALUE_NOT_SET: FALLTHROUGH_INTENDED;
    case InternalType::kListValue: FALLTHROUGH_INTENDED;
    case InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case InternalType::kFrozenValue: FALLTHROUGH_INTENDED;
    case InternalType::kTupleValue:
      // Postgres does not have these datatypes.
      return STATUS_FORMAT(Corruption,
          "Unexpected data was read from database: col_value.type()=$0", col_value.value_case());
    case InternalType::kGinNullValue:
      PgWriteInt<uint8_t>(col_value.gin_null_value(), buffer);
  }

  return Status::OK();
}

template <class Value>
constexpr size_t PgWireIntSize() {
  return PgWireDataHeader::kSerializedSize + sizeof(Value);
}

template <bool kNullTerminated>
size_t PgWireBytesSize(size_t value_size) {
  return PgWireIntSize<uint64_t>() + value_size + (kNullTerminated ? 1 : 0);
}

template <class Value>
Result<size_t> DoColumnWireSize(const Value& col_value) {
  if (IsNull(col_value)) {
    return ColumnWireSize(DataType::NULL_VALUE_TYPE, /* is_null= */ true, /* payload_size= */ 0);
  }

  switch (col_value.value_case()) {
    case InternalType::kBoolValue:
      return ColumnWireSize(DataType::BOOL, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kInt8Value:
      return ColumnWireSize(DataType::INT8, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kInt16Value:
      return ColumnWireSize(DataType::INT16, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kInt32Value:
      return ColumnWireSize(DataType::INT32, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kInt64Value:
      return ColumnWireSize(DataType::INT64, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kUint32Value:
      return ColumnWireSize(DataType::UINT32, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kUint64Value:
      return ColumnWireSize(DataType::UINT64, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kFloatValue:
      return ColumnWireSize(DataType::FLOAT, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kDoubleValue:
      return ColumnWireSize(DataType::DOUBLE, /* is_null= */ false, /* payload_size= */ 0);
    case InternalType::kStringValue:
      return ColumnWireSize(
          DataType::STRING, /* is_null= */ false, col_value.string_value().size());
    case InternalType::kBinaryValue:
      return ColumnWireSize(
          DataType::BINARY, /* is_null= */ false, col_value.binary_value().size());
    case InternalType::kDecimalValue:
      return ColumnWireSize(
          DataType::DECIMAL, /* is_null= */ false, col_value.decimal_value().size());
    case InternalType::kBsonValue:
      return ColumnWireSize(DataType::BSON, /* is_null= */ false, col_value.bson_value().size());
    case InternalType::kGinNullValue:
      return ColumnWireSize(DataType::GIN_NULL, /* is_null= */ false, /* payload_size= */ 0);

    case InternalType::kVirtualValue:
    case InternalType::kTimestampValue:
    case InternalType::kDateValue:
    case InternalType::kTimeValue:
    case InternalType::kVarintValue:
    case InternalType::kInetaddressValue:
    case InternalType::kJsonbValue:
    case InternalType::kUuidValue:
    case InternalType::kTimeuuidValue:
      return STATUS_FORMAT(NotSupported,
          "Unexpected data was read from database: col_value.type()=$0", col_value.value_case());

    case InternalType::VALUE_NOT_SET: FALLTHROUGH_INTENDED;
    case InternalType::kListValue: FALLTHROUGH_INTENDED;
    case InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case InternalType::kFrozenValue: FALLTHROUGH_INTENDED;
    case InternalType::kTupleValue:
      return STATUS_FORMAT(Corruption,
          "Unexpected data was read from database: col_value.type()=$0", col_value.value_case());
  }

  return STATUS_FORMAT(Corruption,
      "Unexpected data was read from database: col_value.type()=$0", col_value.value_case());
}

} // namespace

Status WriteColumn(const QLValuePB& col_value, WriteBuffer* buffer) {
  return DoWriteColumn(col_value, buffer);
}

Status WriteColumn(const QLValuePB& col_value, ValueBuffer* buffer) {
  return DoWriteColumn(col_value, buffer);
}

Status WriteColumn(const LWQLValuePB& col_value, WriteBuffer* buffer) {
  return DoWriteColumn(col_value, buffer);
}

Status WriteColumn(const LWQLValuePB& col_value, ValueBuffer* buffer) {
  return DoWriteColumn(col_value, buffer);
}

void WriteBinaryColumn(const Slice& col_value, WriteBuffer* buffer) {
  PgWriteBytes</* null_terminating= */ false>(col_value, buffer);
}

void WriteNullColumn(WriteBuffer* buffer) {
  PgWriteNull(buffer);
}

Result<size_t> ColumnWireSize(const QLValuePB& col_value) {
  return DoColumnWireSize(col_value);
}

Result<size_t> ColumnWireSize(const LWQLValuePB& col_value) {
  return DoColumnWireSize(col_value);
}

Result<size_t> ColumnWireSize(DataType type, bool is_null, size_t payload_size) {
  if (is_null) {
    return PgWireDataHeader::kSerializedSize;
  }

  switch (type) {
    case DataType::BOOL: FALLTHROUGH_INTENDED;
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::GIN_NULL:
      return PgWireIntSize<uint8_t>();
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::UINT16:
      return PgWireIntSize<int16_t>();
    case DataType::INT32: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::FLOAT:
      return PgWireIntSize<int32_t>();
    case DataType::INT64: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::DOUBLE:
      return PgWireIntSize<int64_t>();
    case DataType::STRING: FALLTHROUGH_INTENDED;
    case DataType::DECIMAL:
      return PgWireBytesSize</* null_terminating= */ true>(payload_size);
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::BSON: FALLTHROUGH_INTENDED;
    case DataType::VECTOR:
      return PgWireBytesSize</* null_terminating= */ false>(payload_size);

    case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case DataType::TIMESTAMP: FALLTHROUGH_INTENDED;
    case DataType::DATE: FALLTHROUGH_INTENDED;
    case DataType::TIME: FALLTHROUGH_INTENDED;
    case DataType::VARINT: FALLTHROUGH_INTENDED;
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
    case DataType::JSONB:
      return STATUS_FORMAT(NotSupported, "Unexpected data type was read from database: $0", type);
  }
  return STATUS_FORMAT(NotSupported, "Unexpected data type was read from database: $0", type);
}

bool IsFixedWidthDataType(DataType type) {
  switch (type) {
    case DataType::BOOL: FALLTHROUGH_INTENDED;
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::GIN_NULL: FALLTHROUGH_INTENDED;
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::INT32: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::FLOAT: FALLTHROUGH_INTENDED;
    case DataType::INT64: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::DOUBLE:
      return true;
    default:
      return false;
  }
}

bool IsFixedWidthAggregate(bfpg::TSOpcode opcode, DataType arg_type) {
  switch (opcode) {
    case bfpg::TSOpcode::kCount:
    case bfpg::TSOpcode::kSumInt8:
    case bfpg::TSOpcode::kSumInt16:
    case bfpg::TSOpcode::kSumInt32:
    case bfpg::TSOpcode::kSumInt64:
    case bfpg::TSOpcode::kSumFloat:
    case bfpg::TSOpcode::kSumDouble:
      return true;
    case bfpg::TSOpcode::kMin:
    case bfpg::TSOpcode::kMax:
      return IsFixedWidthDataType(arg_type);
    default:
      return false;
  }
}

bool IsSupportedGroupKeyType(DataType type) {
  switch (type) {
    case DataType::BOOL: FALLTHROUGH_INTENDED;
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::INT32: FALLTHROUGH_INTENDED;
    case DataType::INT64: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::FLOAT: FALLTHROUGH_INTENDED;
    case DataType::DOUBLE: FALLTHROUGH_INTENDED;
    case DataType::DECIMAL: FALLTHROUGH_INTENDED;
    case DataType::STRING: FALLTHROUGH_INTENDED;
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::BSON:
      return true;
    default:
      return false;
  }
}

//--------------------------------------------------------------------------------------------------
// Read Tuple Routine in DocDB Format (wire_protocol).
//--------------------------------------------------------------------------------------------------

void PgDocData::LoadCache(const Slice& cache, int64_t *total_row_count, Slice *cursor) {
  *cursor = cache;

  // Read the number row_count in this set.
  *total_row_count = ReadNumber<int64_t>(cursor);
}

}  // namespace pggate
}  // namespace yb
