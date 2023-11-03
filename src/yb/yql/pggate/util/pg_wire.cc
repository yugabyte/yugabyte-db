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

#include "yb/yql/pggate/util/pg_wire.h"

namespace yb {
namespace pggate {

void PgWire::WriteBool(bool value, WriteBuffer *buffer) {
  buffer->Append(pointer_cast<const char*>(&value), sizeof(bool));
}

void PgWire::WriteInt8(int8_t value, WriteBuffer *buffer) {
  buffer->Append(pointer_cast<const char*>(&value), sizeof(int8_t));
}

void PgWire::WriteUint8(uint8_t value, WriteBuffer *buffer) {
  buffer->Append(pointer_cast<const char*>(&value), sizeof(uint8_t));
}

void PgWire::WriteUint16(uint16_t value, WriteBuffer *buffer) {
  WriteInt(NetworkByteOrder::Store16, value, buffer);
}

void PgWire::WriteInt16(int16_t value, WriteBuffer *buffer) {
  WriteInt(NetworkByteOrder::Store16, static_cast<uint16>(value), buffer);
}

void PgWire::WriteUint32(uint32_t value, WriteBuffer *buffer) {
  WriteInt(NetworkByteOrder::Store32, value, buffer);
}

void PgWire::WriteInt32(int32_t value, WriteBuffer *buffer) {
  WriteInt(NetworkByteOrder::Store32, static_cast<uint32>(value), buffer);
}

void PgWire::WriteUint64(uint64_t value, WriteBuffer *buffer) {
  WriteInt(NetworkByteOrder::Store64, value, buffer);
}

void PgWire::WriteInt64(int64_t value, WriteBuffer *buffer) {
  WriteInt(NetworkByteOrder::Store64, static_cast<uint64>(value), buffer);
}

void PgWire::WriteFloat(float value, WriteBuffer *buffer) {
  const uint32 int_value = *reinterpret_cast<const uint32*>(&value);
  WriteInt(NetworkByteOrder::Store32, int_value, buffer);
}

void PgWire::WriteDouble(double value, WriteBuffer *buffer) {
  const uint64 int_value = *reinterpret_cast<const uint64*>(&value);
  WriteInt(NetworkByteOrder::Store64, int_value, buffer);
}

void PgWire::WriteText(const std::string& value, WriteBuffer *buffer) {
  // Postgres expected text string to be null-terminated, so we have to add '\0' here.
  // Postgres will call strlen() without using the returning byte count.
  const uint64 length = value.size() + 1;
  WriteInt(NetworkByteOrder::Store64, length, buffer);
  buffer->Append(value.c_str(), length);
}

void PgWire::WriteBinary(const std::string& value, WriteBuffer *buffer) {
  const uint64 length = value.size();
  WriteInt(NetworkByteOrder::Store64, length, buffer);
  buffer->Append(value.c_str(), value.size());
}

// Read Text Data
void PgWire::ReadBytes(Slice *cursor, char *value, int64_t bytes) {
  memcpy(value, cursor->data(), bytes);
  cursor->remove_prefix(bytes);
}

// Read Text data into string
void PgWire::ReadString(Slice *cursor, std::string *value, int64_t bytes) {
  value->assign(cursor->cdata(), bytes);
  cursor->remove_prefix(bytes);
}

}  // namespace pggate
}  // namespace yb
