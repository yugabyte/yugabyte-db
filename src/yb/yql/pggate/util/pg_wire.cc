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

#include "yb/gutil/endian.h"

namespace yb {
namespace pggate {

void PgWire::WriteBool(bool value, faststring *buffer) {
  buffer->append(&value, sizeof(bool));
}

void PgWire::WriteInt8(int8_t value, faststring *buffer) {
  buffer->append(&value, sizeof(int8_t));
}

void PgWire::WriteUint8(uint8_t value, faststring *buffer) {
  buffer->append(&value, sizeof(uint8_t));
}

void PgWire::WriteUint16(uint16_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store16, value, buffer);
}

void PgWire::WriteInt16(int16_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store16, static_cast<uint16>(value), buffer);
}

void PgWire::WriteUint32(uint32_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store32, value, buffer);
}

void PgWire::WriteInt32(int32_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store32, static_cast<uint32>(value), buffer);
}

void PgWire::WriteUint64(uint64_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store64, value, buffer);
}

void PgWire::WriteInt64(int64_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store64, static_cast<uint64>(value), buffer);
}

void PgWire::WriteFloat(float value, faststring *buffer) {
  const uint32 int_value = *reinterpret_cast<const uint32*>(&value);
  WriteInt(NetworkByteOrder::Store32, int_value, buffer);
}

void PgWire::WriteDouble(double value, faststring *buffer) {
  const uint64 int_value = *reinterpret_cast<const uint64*>(&value);
  WriteInt(NetworkByteOrder::Store64, int_value, buffer);
}

void PgWire::WriteText(const std::string& value, faststring *buffer) {
  // Postgres expected text string to be null-terminated, so we have to add '\0' here.
  // Postgres will call strlen() without using the returning byte count.
  const uint64 length = value.size() + 1;
  WriteInt(NetworkByteOrder::Store64, length, buffer);
  buffer->append(static_cast<const void *>(value.c_str()), length);
}

void PgWire::WriteBinary(const std::string& value, faststring *buffer) {
  const uint64 length = value.size();
  WriteInt(NetworkByteOrder::Store64, length, buffer);
  buffer->append(value);
}

//--------------------------------------------------------------------------------------------------
// Read numbers.

// This is not called ReadBool but ReadNumber because it is invoked from the TranslateNumber
// template function similarly to the rest of numeric types.
size_t PgWire::ReadNumber(Slice *cursor, bool *value) {
  *value = !!*reinterpret_cast<const bool*>(cursor->data());
  return sizeof(bool);
}

size_t PgWire::ReadNumber(Slice *cursor, int8_t *value) {
  *value = *reinterpret_cast<const int8_t*>(cursor->data());
  return sizeof(int8_t);
}

size_t PgWire::ReadNumber(Slice *cursor, uint8_t *value) {
  *value = *reinterpret_cast<const uint8*>(cursor->data());
  return sizeof(uint8_t);
}

size_t PgWire::ReadNumber(Slice *cursor, uint16 *value) {
  return ReadNumericValue(NetworkByteOrder::Load16, cursor, value);
}

size_t PgWire::ReadNumber(Slice *cursor, int16 *value) {
  return ReadNumericValue(NetworkByteOrder::Load16, cursor, reinterpret_cast<uint16*>(value));
}

size_t PgWire::ReadNumber(Slice *cursor, uint32 *value) {
  return ReadNumericValue(NetworkByteOrder::Load32, cursor, value);
}

size_t PgWire::ReadNumber(Slice *cursor, int32 *value) {
  return ReadNumericValue(NetworkByteOrder::Load32, cursor, reinterpret_cast<uint32*>(value));
}

size_t PgWire::ReadNumber(Slice *cursor, uint64 *value) {
  return ReadNumericValue(NetworkByteOrder::Load64, cursor, value);
}

size_t PgWire::ReadNumber(Slice *cursor, int64 *value) {
  return ReadNumericValue(NetworkByteOrder::Load64, cursor, reinterpret_cast<uint64*>(value));
}

size_t PgWire::ReadNumber(Slice *cursor, float *value) {
  uint32 int_value;
  size_t read_size = ReadNumericValue(NetworkByteOrder::Load32, cursor, &int_value);
  *value = *reinterpret_cast<float*>(&int_value);
  return read_size;
}

size_t PgWire::ReadNumber(Slice *cursor, double *value) {
  uint64 int_value;
  size_t read_size = ReadNumericValue(NetworkByteOrder::Load64, cursor, &int_value);
  *value = *reinterpret_cast<double*>(&int_value);
  return read_size;
}

// Read Text Data
size_t PgWire::ReadBytes(Slice *cursor, char *value, int64_t bytes) {
  memcpy(value, cursor->data(), bytes);
  return bytes;
}

// Read Text data into string
size_t PgWire::ReadString(Slice *cursor, std::string *value, int64_t bytes) {
  value->assign(cursor->cdata(), bytes);
  return bytes;
}

}  // namespace pggate
}  // namespace yb
