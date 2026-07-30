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

namespace yb::pggate {

PgTuple::PgTuple(uint64_t* datums, bool* isnulls, YbcPgSysColumns* syscols, size_t nattrs)
    : datums_(datums), isnulls_(isnulls), syscols_(syscols) {
  DEBUG_ONLY(nattrs_ = nattrs);
}

void PgTuple::CopyFrom(const PgTuple& other, size_t nattrs) {
  DEBUG_ONLY(DCHECK_LE(nattrs, nattrs_)  << "PgTuple index is out of bounds");
  memcpy(datums_, other.datums_, nattrs * sizeof(*datums_));
  memcpy(isnulls_, other.isnulls_, nattrs * sizeof(*isnulls_));
  if (syscols_ && other.syscols_) {
    memcpy(syscols_, other.syscols_, sizeof(YbcPgSysColumns));
  }
}

void PgTuple::DoWrite(int index, bool isnull, uint64_t datum) {
  DEBUG_ONLY(DCHECK_LT(index, nattrs_) << "PgTuple index is out of bounds");
  isnulls_[index] = isnull;
  datums_[index] = datum;
}

}  // namespace yb::pggate
