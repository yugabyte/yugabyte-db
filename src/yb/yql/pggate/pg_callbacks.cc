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

#include "yb/yql/pggate/pg_callbacks.h"

#include <string>

#include "yb/util/logging.h"

namespace yb {
namespace pggate {

std::string GetDebugQueryString(const PgCallbacks& callbacks) {
  DCHECK_ONLY_NOTNULL(callbacks.GetDebugQueryString);
  const char* query_str = callbacks.GetDebugQueryString();
  if (query_str) {
    return query_str;
  }
  return "No query";
}

}  // namespace pggate
}  // namespace yb
