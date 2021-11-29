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

#ifndef YB_YQL_PGGATE_UTIL_PG_WIRE_H_
#define YB_YQL_PGGATE_UTIL_PG_WIRE_H_

#include <bitset>
#include "yb/util/slice.h"

namespace yb {
namespace pggate {

// This class represent how YugaByte sends data over the wire. See also file
// "yb/common/wire_protocol.proto".
//
// TODO(neil) Consider moving this file to "yb/common" directory and merging and organizing with
// the existing functions in "wire_protocol.*" accordingly.
class PgWire {
 public:
  //------------------------------------------------------------------------------------------------
  // Read Numeric Data
  template<typename num_type>
  static size_t ReadNumericValue(num_type (*reader)(const void*), Slice *cursor, num_type *value) {
    *value = reader(cursor->data());
    return sizeof(num_type);
  }

  static size_t ReadNumber(Slice *cursor, bool *value);
  static size_t ReadNumber(Slice *cursor, uint8 *value);
  static size_t ReadNumber(Slice *cursor, int8 *value);
  static size_t ReadNumber(Slice *cursor, uint16 *value);
  static size_t ReadNumber(Slice *cursor, int16 *value);
  static size_t ReadNumber(Slice *cursor, uint32 *value);
  static size_t ReadNumber(Slice *cursor, int32 *value);
  static size_t ReadNumber(Slice *cursor, uint64 *value);
  static size_t ReadNumber(Slice *cursor, int64 *value);
  static size_t ReadNumber(Slice *cursor, float *value);
  static size_t ReadNumber(Slice *cursor, double *value);

  // Read Text Data
  static size_t ReadBytes(Slice *cursor, char *value, int64_t bytes);
  static size_t ReadString(Slice *cursor, std::string *value, int64_t bytes);

  //------------------------------------------------------------------------------------------------
  // Write Numeric Data
  template<typename num_type>
  static void WriteInt(void (*writer)(void *, num_type), num_type value, faststring *buffer) {
    num_type bytes;
    writer(&bytes, value);
    buffer->append(&bytes, sizeof(num_type));
  }

  static void WriteBool(bool value, faststring *buffer);
  static void WriteInt8(int8_t value, faststring *buffer);
  static void WriteUint8(uint8_t value, faststring *buffer);
  static void WriteUint16(uint16_t value, faststring *buffer);
  static void WriteInt16(int16_t value, faststring *buffer);
  static void WriteUint32(uint32_t value, faststring *buffer);
  static void WriteInt32(int32_t value, faststring *buffer);
  static void WriteUint64(uint64_t value, faststring *buffer);
  static void WriteInt64(int64_t value, faststring *buffer);
  static void WriteFloat(float value, faststring *buffer);
  static void WriteDouble(double value, faststring *buffer);

  // Write Text Data
  static void WriteText(const std::string& value, faststring *buffer);

  // Write Text Data
  static void WriteBinary(const std::string& value, faststring *buffer);
};

// Just in case we change the serialization format. Different versions of DocDB and Postgres
// support can work with multiple data formats.
// - Rolling upgrade will need this.
// - TODO(neil) yb_client should have information on what pg_format_version should be used.
// GFLAG for CurrentDataFormatVersion;

// Data Header on the wire.
// We'll use one byte to represent column metadata.
//   Bit 0x01 - NULL indicator
//   Bit 0x02 - unused
//   Bit 0x04 - unused
//   Bit 0x08 - unused
//   ....
class PgWireDataHeader {
 public:
  PgWireDataHeader() {
  }

  explicit PgWireDataHeader(uint8_t val) : data_(val) {
  }

  void set_null() {
    data_[0] = 1;
  }
  bool is_null() const {
    return data_[0] == 1;
  }

  uint8_t ToUint8() const {
    return static_cast<uint8_t>(data_.to_ulong());
  }

 private:
  std::bitset<8> data_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_WIRE_H_
