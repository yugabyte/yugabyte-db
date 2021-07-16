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

// Various utilities for calling callbacks exposed by PostgreSQL code from C++ code.

#ifndef YB_YQL_PGGATE_PG_CALLBACKS_H
#define YB_YQL_PGGATE_PG_CALLBACKS_H

#include <string>

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {

// Get current SQL query string in a form suitable for logging.
std::string GetDebugQueryString(const PgCallbacks& callbacks);

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_CALLBACKS_H
