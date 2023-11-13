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

#include "yb/util/logging.h"

#include "yb/common/ybc-internal.h"

namespace yb {
namespace pggate {

PgTuple::PgTuple(uint64_t *datums, bool *isnulls, PgSysColumns *syscols)
    : datums_(datums), isnulls_(isnulls), syscols_(syscols) {
}

void PgTuple::WriteNull(int index, const PgWireDataHeader& header) {
  isnulls_[index] = true;
  datums_[index] = 0;
}

void PgTuple::WriteDatum(int index, uint64_t datum) {
  isnulls_[index] = false;
  datums_[index] = datum;
}

void PgTuple::Write(uint8_t **pgbuf, const PgWireDataHeader& header, const uint8_t *value,
                    int64_t bytes) {
  // TODO: return a status instead of crashing.
  CHECK_LE(bytes, kYBCMaxPostgresTextSizeBytes);
  CHECK_GE(bytes, 0);
  *pgbuf = static_cast<uint8_t*>(YBCCStringToTextWithLen(reinterpret_cast<const char*>(value),
                                                         static_cast<int>(bytes)));
}

}  // namespace pggate
}  // namespace yb
