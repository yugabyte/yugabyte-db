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

#ifndef YB_YQL_PGGATE_UTIL_PG_NET_WRITER_H_
#define YB_YQL_PGGATE_UTIL_PG_NET_WRITER_H_

#include "yb/util/bytes_formatter.h"
#include "yb/common/pgsql_resultset.h"

namespace yb {
namespace pggate {

// Just in case we change the serialization format. Different versions of DocDB and Postgres
// support can work with multiple data formats.
// - Rolling upgrade will need this.
// - TODO(neil) yb_client should have information on what pg_format_version should be used.
static const int kCurrentDataFormatVersion = 1;

class PgNetWriter {
 public:
  static CHECKED_STATUS WriteTuples(const PgsqlResultSet& tuples, faststring *buffer);

  static CHECKED_STATUS WriteTuple(const PgsqlRSRow& tuple, faststring *buffer);

  static CHECKED_STATUS WriteColumn(const QLValue& col_value, faststring *buffer);

 private:
  // Write Numeric Data ----------------------------------------------------------------------------
  template<typename num_type>
  static void WriteInt(void (*writer)(void *, num_type), num_type value, faststring *buffer) {
    num_type bytes;
    writer(&bytes, value);
    buffer->append(&bytes, sizeof(num_type));
  }

  static void WriteUint8(uint8_t value, faststring *buffer);
  static void WriteInt16(int16_t value, faststring *buffer);
  static void WriteInt32(int32_t value, faststring *buffer);
  static void WriteInt64(int64_t value, faststring *buffer);
  static void WriteFloat(float value, faststring *buffer);
  static void WriteDouble(double value, faststring *buffer);

  // Write Text Data -------------------------------------------------------------------------------
  static void WriteText(const string& value, faststring *buffer);
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_NET_WRITER_H_
