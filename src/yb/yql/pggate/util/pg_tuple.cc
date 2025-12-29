//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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

#include "yb/yql/pggate/util/ybc-internal.h"

namespace yb {
namespace pggate {

#ifdef PGTUPLE_DEBUG
PgTuple::PgTuple(size_t nattrs, uint64_t *datums, bool *isnulls, YbcPgSysColumns *syscols)
    : nattrs_(nattrs), datums_(datums), isnulls_(isnulls), syscols_(syscols) {
}
#else
PgTuple::PgTuple(uint64_t *datums, bool *isnulls, YbcPgSysColumns *syscols)
    : datums_(datums), isnulls_(isnulls), syscols_(syscols) {
}
#endif

void PgTuple::CopyFrom(const PgTuple& other, size_t nattrs) {
#ifdef PGTUPLE_DEBUG
  CHECK_LE(nattrs, nattrs_) << "PgTuple index is out of bounds";
#endif
  memcpy(datums_, other.datums_, nattrs * sizeof(*datums_));
  memcpy(isnulls_, other.isnulls_, nattrs * sizeof(*isnulls_));
  if (syscols_ && other.syscols_) {
    memcpy(syscols_, other.syscols_, sizeof(YbcPgSysColumns));
  }
}

void PgTuple::WriteNull(int index) {
#ifdef PGTUPLE_DEBUG
  CHECK_LT(index, nattrs_) << "PgTuple index is out of bounds";
#endif
  isnulls_[index] = true;
  datums_[index] = 0;
}

void PgTuple::WriteDatum(int index, uint64_t datum) {
#ifdef PGTUPLE_DEBUG
  CHECK_LT(index, nattrs_) << "PgTuple index is out of bounds";
#endif
  isnulls_[index] = false;
  datums_[index] = datum;
}

}  // namespace pggate
}  // namespace yb
