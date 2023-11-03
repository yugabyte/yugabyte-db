//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/util/pg_doc_data.h"

#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

#include "yb/util/format.h"
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

template <bool NullTerminated, class Buffer>
void PgWriteBytes(const Slice& value, Buffer* buffer) {
  auto length = value.size() + NullTerminated;
  PgWriteInt<uint64_t>(length, buffer);
  buffer->Append(value.cdata(), length);
}

template <class Buffer>
Status DoWriteColumn(const QLValuePB& col_value, Buffer* buffer) {
  // Write data header.
  if (QLValue::IsNull(col_value)) {
    PgWireDataHeader header;
    header.set_null();
    char buf[PgWireDataHeader::kSerializedSize];
    header.SerializeTo(buf);
    buffer->Append(buf, sizeof(buf));
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

} // namespace

Status WriteColumn(const QLValuePB& col_value, WriteBuffer* buffer) {
  return DoWriteColumn(col_value, buffer);
}

Status WriteColumn(const QLValuePB& col_value, ValueBuffer* buffer) {
  return DoWriteColumn(col_value, buffer);
}

void WriteBinaryColumn(const Slice& col_value, WriteBuffer* buffer) {
  PgWriteBytes</* null_terminating= */ false>(col_value, buffer);
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
