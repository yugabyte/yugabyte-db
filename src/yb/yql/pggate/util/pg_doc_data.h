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

#pragma once

#include "yb/common/common_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/yql/pggate/util/pg_wire.h"

#include "yb/util/kv_util.h"

namespace yb {
namespace pggate {

Status WriteColumn(const QLValuePB& col_value, WriteBuffer* buffer);
Status WriteColumn(const QLValuePB& col_value, ValueBuffer* buffer);

void WriteBinaryColumn(const Slice& col_value, WriteBuffer* buffer);

class PgDocData : public PgWire {
 public:
  static void LoadCache(const Slice& cache, int64_t* total_row_count, Slice* cursor);

  static bool ReadHeaderIsNull(Slice *cursor) {
    return cursor->consume_byte() != 0;
  }
};

}  // namespace pggate
}  // namespace yb
