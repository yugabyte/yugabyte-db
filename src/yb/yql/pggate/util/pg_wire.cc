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

void PgWire::WriteUint8(uint8_t value, faststring *buffer) {
  buffer->append(&value, sizeof(uint8_t));
}

void PgWire::WriteInt16(int16_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store16, static_cast<uint16>(value), buffer);
}

void PgWire::WriteInt32(int32_t value, faststring *buffer) {
  WriteInt(NetworkByteOrder::Store32, static_cast<uint32>(value), buffer);
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

void PgWire::WriteText(const string& value, faststring *buffer) {
  const uint64 length = value.size();
  WriteInt(NetworkByteOrder::Store64, length, buffer);
  buffer->append(value);
}

//--------------------------------------------------------------------------------------------------
// Read numbers.
size_t PgWire::ReadNumber(Slice *cursor, uint8 *value) {
  *value = *reinterpret_cast<const uint8*>(cursor->data());
  return sizeof(uint8_t);
}

size_t PgWire::ReadNumber(Slice *cursor, int16 *value) {
  return ReadNumericValue(NetworkByteOrder::Load16, cursor, reinterpret_cast<uint16*>(value));
}

size_t PgWire::ReadNumber(Slice *cursor, int32 *value) {
  return ReadNumericValue(NetworkByteOrder::Load32, cursor, reinterpret_cast<uint32*>(value));
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

}  // namespace pggate
}  // namespace yb
