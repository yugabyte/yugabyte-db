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

#ifndef YB_YQL_PGGATE_UTIL_PG_NET_READER_H_
#define YB_YQL_PGGATE_UTIL_PG_NET_READER_H_

#include "yb/util/bytes_formatter.h"
#include "yb/common/pgsql_resultset.h"
#include "yb/yql/pggate/util/pg_net_data.h"

namespace yb {
namespace pggate {

class PgBind;
class PgNetReader {
 public:
  static CHECKED_STATUS ReadNewTuples(const string& net_data,
                                      int64_t *total_row_count,
                                      PgNetBuffer *buffer);
  static CHECKED_STATUS ReadTuple(const vector<std::shared_ptr<PgBind>>& pg_binds,
                                  PgNetBuffer *buffer);
  static CHECKED_STATUS ReadColumn(const std::shared_ptr<PgBind>& pg_bind, PgNetBuffer *buffer);

  // Read Numeric Data ----------------------------------------------------------------------------
  template<typename num_type>
  static size_t ReadNumericValue(num_type (*reader)(const void*), PgNetBuffer *buffer,
                                 num_type *value) {
    *value = reader(buffer->data());
    return sizeof(num_type);
  }

  static size_t ReadNumber(PgNetBuffer *buffer, uint8 *value);
  static size_t ReadNumber(PgNetBuffer *buffer, int16 *value);
  static size_t ReadNumber(PgNetBuffer *buffer, int32 *value);
  static size_t ReadNumber(PgNetBuffer *buffer, int64 *value);
  static size_t ReadNumber(PgNetBuffer *buffer, float *value);
  static size_t ReadNumber(PgNetBuffer *buffer, double *value);

  // Read Text Data -------------------------------------------------------------------------------
  static size_t ReadBytes(PgNetBuffer *buffer, char *value, int64_t bytes);
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_NET_READER_H_
