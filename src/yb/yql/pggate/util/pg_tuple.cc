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

#include "yb/yql/pggate/util/pg_tuple.h"

#include "yb/util/ybc-internal.h"
#include "yb/client/client.h"

namespace yb {
namespace pggate {

PgTuple::PgTuple(uint64_t *datums, bool *isnulls) : datums_(datums), isnulls_(isnulls) {
}

void PgTuple::WriteNull(int index, const PgWireDataHeader& header) {
  isnulls_[index] = true;
  datums_[index] = 0;
}

void PgTuple::Write(int index, const PgWireDataHeader& header, bool value) {
  isnulls_[index] = false;
  datums_[index] = value ? 1 : 0;
}

void PgTuple::Write(int index, const PgWireDataHeader& header, int16_t value) {
  isnulls_[index] = false;
  datums_[index] = value;
}

void PgTuple::Write(int index, const PgWireDataHeader& header, int32_t value) {
  isnulls_[index] = false;
  datums_[index] = value;
}

void PgTuple::Write(int index, const PgWireDataHeader& header, int64_t value) {
  isnulls_[index] = false;
  datums_[index] = value;
}

void PgTuple::Write(int index, const PgWireDataHeader& header, float value) {
  isnulls_[index] = false;
  datums_[index] = *reinterpret_cast<uint32_t*>(&value);
}

void PgTuple::Write(int index, const PgWireDataHeader& header, double value) {
  isnulls_[index] = false;
  datums_[index] = *reinterpret_cast<uint64_t*>(&value);
}

void PgTuple::Write(int index, const PgWireDataHeader& header, const char *value, int64_t bytes) {
  isnulls_[index] = false;

  // PostgreSQL can represent text strings up to 1 GB minus a four-byte header.
  const int64_t kMaxPostgresTextSizeBytes = 1024ll * 1024 * 1024 - 4;
  // TODO: return a status instead of crashing.
  CHECK_LE(bytes, kMaxPostgresTextSizeBytes);
  CHECK_GE(bytes, 0);
  datums_[index] = (uint64_t) YBCCStringToTextWithLen(value, static_cast<int>(bytes));
}

// TODO(neil) Once we serialize and deserialize binary types on Postgres side, we can implement
// this function properly. Raise exception for now.
void PgTuple::Write(int index, const PgWireDataHeader& header, const uint8_t *value,
                    int64_t bytes) {
  isnulls_[index] = false;
  LOG(FATAL) << "Not yet supported";
}

}  // namespace pggate
}  // namespace yb
