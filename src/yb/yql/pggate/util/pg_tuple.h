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

#include "yb/util/debug-util.h"

namespace yb::pggate {

// PgTuple.
// TODO(neil) This code needs to be optimize. We might be able to use DocDB buffer directly for
// most datatype except numeric. A simpler optimization would be allocate one buffer for each
// tuple and write the value there.
//
// Currently we allocate one individual buffer per column and write result there.
class PgTuple {
 public:
  PgTuple(uint64_t* datums, bool* isnulls, YbcPgSysColumns* syscols, size_t nattrs);

  void CopyFrom(const PgTuple& other, size_t nattrs);

  void WriteNull(int index) { DoWrite(index); }

  void WriteDatum(int index, uint64_t datum) { DoWrite(index, /* isnull= */ false, datum); }

  // Get returning-space for system columns. Tuple writer will save values in this struct.
  YbcPgSysColumns* syscols() { return syscols_; }

 private:
  void DoWrite(int index, bool isnull = true, uint64_t datum = 0);

  uint64_t* datums_;
  bool* isnulls_;
  YbcPgSysColumns* syscols_;
  DEBUG_ONLY(size_t nattrs_);
};

}  // namespace yb::pggate
