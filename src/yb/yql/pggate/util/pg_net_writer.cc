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

#include "yb/yql/pggate/util/pg_net_writer.h"

#include "yb/yql/pggate/util/pg_net_data.h"
#include "yb/client/client.h"

namespace yb {
namespace pggate {

Status PgNetWriter::WriteTuples(const PgsqlResultSet& tuples, faststring *buffer) {
  // Write the number rows.
  WriteInt64(tuples.rsrow_count(), buffer);

  // Write the row contents.
  for (const PgsqlRSRow& tuple : tuples.rsrows()) {
    RETURN_NOT_OK(WriteTuple(tuple, buffer));
  }
  return Status::OK();
}

Status PgNetWriter::WriteTuple(const PgsqlRSRow& tuple, faststring *buffer) {
  // Write the column contents.
  for (const QLValue& col_value : tuple.rscols()) {
    RETURN_NOT_OK(WriteColumn(col_value, buffer));
  }
  return Status::OK();
}

Status PgNetWriter::WriteColumn(const QLValue& col_value, faststring *buffer) {
  // Write data header.
  bool has_data = true;
  PgColumnHeader col_header;
  if (col_value.IsNull()) {
    col_header.set_null();
    has_data = false;
  }
  WriteUint8(col_header.ToUint8(), buffer);

  if (!has_data) {
    return Status::OK();
  }

  switch (col_value.type()) {
    case InternalType::VALUE_NOT_SET:
      break;
    case InternalType::kInt16Value:
      WriteInt16(col_value.int16_value(), buffer);
      break;
    case InternalType::kInt32Value:
      WriteInt32(col_value.int32_value(), buffer);
      break;
    case InternalType::kInt64Value:
      WriteInt64(col_value.int64_value(), buffer);
      break;
    case InternalType::kFloatValue:
      WriteFloat(col_value.float_value(), buffer);
      break;
    case InternalType::kDoubleValue:
      WriteDouble(col_value.double_value(), buffer);
      break;
    case InternalType::kStringValue:
      WriteText(col_value.string_value(), buffer);
      break;

    case InternalType::kBoolValue:
    case InternalType::kBinaryValue:
    case InternalType::kTimestampValue:
    case InternalType::kDecimalValue:
    case InternalType::kVarintValue:
    case InternalType::kInetaddressValue:
    case InternalType::kJsonbValue:
    case InternalType::kUuidValue:
    case InternalType::kTimeuuidValue:
      // PgGate has not supported these datatypes yet.
      return STATUS(NotSupported, "Unexpected data was read from database");

    case InternalType::kInt8Value:
    case InternalType::kListValue:
    case InternalType::kMapValue:
    case InternalType::kSetValue:
    case InternalType::kFrozenValue:
      // Postgres does not have these datatypes.
      return STATUS(Corruption, "Unexpected data was read from database");
  }

  return Status::OK();
}

void PgNetWriter::WriteUint8(uint8_t value, faststring *buffer) {
  buffer->append(&value, sizeof(uint8_t));
}

void PgNetWriter::WriteInt16(int16_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store16, static_cast<uint16>(value), buffer);
}

void PgNetWriter::WriteInt32(int32_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store32, static_cast<uint32>(value), buffer);
}

void PgNetWriter::WriteInt64(int64_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store64, static_cast<uint64>(value), buffer);
}

void PgNetWriter::WriteFloat(float value, faststring *buffer) {
  const uint32 int_value = *reinterpret_cast<const uint32*>(&value);
  WriteInt(NetworkByteOrder::Store32, int_value, buffer);
}

void PgNetWriter::WriteDouble(double value, faststring *buffer) {
  const uint64 int_value = *reinterpret_cast<const uint64*>(&value);
  WriteInt(NetworkByteOrder::Store64, int_value, buffer);
}

void PgNetWriter::WriteText(const string& value, faststring *buffer) {
  const uint64 length = value.size();
  WriteInt(NetworkByteOrder::Store64, length, buffer);
  buffer->append(value);
}

}  // namespace pggate
}  // namespace yb
