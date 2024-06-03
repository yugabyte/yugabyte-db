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

#include "yb/yql/pggate/util/ybc-internal.h"

namespace yb {
namespace pggate {

PgTuple::PgTuple(uint64_t *datums, bool *isnulls, PgSysColumns *syscols)
    : datums_(datums), isnulls_(isnulls), syscols_(syscols) {
}

void PgTuple::WriteNull(int index) {
  isnulls_[index] = true;
  datums_[index] = 0;
}

void PgTuple::WriteDatum(int index, uint64_t datum) {
  isnulls_[index] = false;
  datums_[index] = datum;
}

}  // namespace pggate
}  // namespace yb
