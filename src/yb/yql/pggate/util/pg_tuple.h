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

#pragma once

#include "yb/yql/pggate/util/pg_wire.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#ifndef NDEBUG
#define PGTUPLE_DEBUG
#endif

namespace yb {
namespace pggate {

// PgTuple.
// TODO(neil) This code needs to be optimize. We might be able to use DocDB buffer directly for
// most datatype except numeric. A simpler optimization would be allocate one buffer for each
// tuple and write the value there.
//
// Currently we allocate one individual buffer per column and write result there.
class PgTuple {
 public:
#ifdef PGTUPLE_DEBUG
  PgTuple(size_t nattrs, uint64_t *datums, bool *isnulls, YbcPgSysColumns *syscols);
#else
  PgTuple(uint64_t *datums, bool *isnulls, YbcPgSysColumns *syscols);
#endif

  void CopyFrom(const PgTuple& other, size_t nattrs);

  // Write null value.
  void WriteNull(int index);

  // Write datum to tuple slot.
  void WriteDatum(int index, uint64_t datum);

  // Get returning-space for system columns. Tuple writer will save values in this struct.
  YbcPgSysColumns *syscols() {
    return syscols_;
  }

 private:
#ifdef PGTUPLE_DEBUG
  size_t nattrs_;
#endif
  uint64_t *datums_;
  bool *isnulls_;
  YbcPgSysColumns *syscols_;
};

}  // namespace pggate
}  // namespace yb
