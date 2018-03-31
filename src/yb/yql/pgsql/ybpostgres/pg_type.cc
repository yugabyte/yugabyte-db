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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ybpostgres/pg_type.h"

#include <limits>

namespace yb {
namespace pgapi {

// TODO(neil) Use the definitions in PostgreSQL file "src/include/catalog/pg_type.h" to construct
// a table of "struct PGType". Replace the following tables with PGType table.

//--------------------------------------------------------------------------------------------------

const PGOid kInvalidPgType = std::numeric_limits<uint32_t>::max();
static const PGOid kPgTypeOids[] = {
  kInvalidPgType,               // NULL_VALUE_TYPE
  kInvalidPgType,               // INT8
  INT2OID,                      // INT16
  INT4OID,                      // INT32
  INT8OID,                      // INT64
  TEXTOID,                      // STRING
  BOOLOID,                      // BOOL
  FLOAT4OID,                    // FLOAT
  FLOAT8OID,                    // DOUBLE
  BYTEAOID,                     // BINARY
  TIMESTAMPOID,                 // TIMESTAMP
  NUMERICOID,                   // DECIMAL
  BYTEAOID,                     // VARINT
  INETOID,                      // INET
  kInvalidPgType,               // LIST
  kInvalidPgType,               // MAP
  kInvalidPgType,               // SET
  UUIDOID,                      // UUID
  TEXTOID,                      // TIMEUUID
  kInvalidPgType,               // TUPLE
  kInvalidPgType,               // TYPEARGS
  kInvalidPgType,               // USER_DEFINED_TYPE
  kInvalidPgType,               // FROZEN
  DATEOID,                      // DATE
  TIMEOID,                      // TIME
  JSONBOID,                     // JSONB
};

PGOid PgTypeId(DataType data_type) {
  int type_index = static_cast<int>(data_type);
  return kPgTypeOids[type_index];
}

//--------------------------------------------------------------------------------------------------

static const int16_t kPgTypeSizeBytes[] = {
  -1,       // NULL_VALUE_TYPE
  1,        // INT8
  2,        // INT16
  4,        // INT32
  8,        // INT64
  -1,       // STRING
  1,        // BOOL
  4,        // FLOAT
  8,        // DOUBLE
  -1,       // BINARY
  8,        // TIMESTAMP
  -1,       // DECIMAL
  -1,       // VARINT
  -1,       // INET
  -1,       // LIST
  -1,       // MAP
  -1,       // SET
  16,       // UUID
  -1,       // TIMEUUID
  -1,       // TUPLE
  -1,       // TYPEARGS
  -1,       // USER_DEFINED_TYPE
  -1,       // FROZEN
  4,        // DATE
  8,        // TIME
  -1,       // JSONB
};

int16_t PgTypeLength(DataType data_type) {
  int type_index = static_cast<int>(data_type);
  return kPgTypeSizeBytes[type_index];
}

}  // namespace pgapi
}  // namespace yb
