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
//

#pragma once

#include <string>

#include "yb/common/hybrid_time.h"
#include "yb/common/pg_types.h"

#include "yb/util/tostring.h"

namespace yb {

// Stamp on writes and commits issued on behalf of a YSQL backend; see OriginationInfoPB.
struct OriginationInfo {
  PgOid database_oid = kPgInvalidOid;
  HybridTime origination_ht = HybridTime::kInvalid;

  bool IsSet() const { return origination_ht.is_valid(); }

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_database_oid(database_oid);
    pb->set_origination_ht(origination_ht.ToUint64());
  }

  std::string ToString() const { return YB_STRUCT_TO_STRING(database_oid, origination_ht); }
};

} // namespace yb
