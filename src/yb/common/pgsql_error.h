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

#ifndef YB_COMMON_PGSQL_ERROR_H
#define YB_COMMON_PGSQL_ERROR_H

#include "yb/util/status_fwd.h"
#include "yb/util/status_ec.h"
#include "yb/util/yb_pg_errcodes.h"

namespace yb {

struct PgsqlErrorTag : IntegralErrorTag<YBPgErrorCode> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 6;

  static std::string ToMessage(Value value) {
    return ToString(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }

};

typedef StatusErrorCodeImpl<PgsqlErrorTag> PgsqlError;

} // namespace yb

#endif // YB_COMMON_PGSQL_ERROR_H
