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

#ifndef YB_YQL_PGGATE_UTIL_PG_DOC_DATA_H_
#define YB_YQL_PGGATE_UTIL_PG_DOC_DATA_H_

#include "yb/common/common_fwd.h"

#include "yb/yql/pggate/util/pg_wire.h"

namespace yb {
namespace pggate {

CHECKED_STATUS WriteColumn(const QLValuePB& col_value, faststring *buffer);

class PgDocData : public PgWire {
 public:
  static void LoadCache(const std::string& data, int64_t *total_row_count, Slice *cursor);

  static PgWireDataHeader ReadDataHeader(Slice *cursor);
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_DOC_DATA_H_
