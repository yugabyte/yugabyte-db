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

#include "yb/bfpg/tserver_opcodes.h"
#include "yb/common/common_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/yql/pggate/util/pg_wire.h"

#include "yb/util/kv_util.h"
#include "yb/util/result.h"

namespace yb {
namespace pggate {

Status WriteColumn(const QLValuePB& col_value, WriteBuffer* buffer);
Status WriteColumn(const QLValuePB& col_value, ValueBuffer* buffer);
Status WriteColumn(const LWQLValuePB& col_value, WriteBuffer* buffer);
Status WriteColumn(const LWQLValuePB& col_value, ValueBuffer* buffer);

void WriteBinaryColumn(const Slice& col_value, WriteBuffer* buffer);
void WriteNullColumn(WriteBuffer* buffer);

// Exact PG wire size of a column value as produced by WriteColumn (no buffer write).
Result<size_t> ColumnWireSize(const QLValuePB& col_value);
Result<size_t> ColumnWireSize(const LWQLValuePB& col_value);
// Wire size from encoding-independent column facts (logical payload_size for varlen types).
Result<size_t> ColumnWireSize(DataType type, bool is_null, size_t payload_size);

// True only for types where ColumnWireSize(type, /*is_null=*/false, 0) succeeds.
bool IsFixedWidthDataType(DataType type);
// COUNT / typed SUM* are always fixed-width; MIN/MAX follow the operand type.
bool IsFixedWidthAggregate(bfpg::TSOpcode opcode, DataType arg_type);
// Group keys must be types PgValue::ToQLValuePB converts; any other type LOG(FATAL)s there.
// VECTOR converts, but grouping by a vector is not supported: its stored value also carries the
// DocDB vector id, which a plain read strips.
bool IsSupportedGroupKeyType(DataType type);

class PgDocData : public PgWire {
 public:
  static void LoadCache(const Slice& cache, int64_t* total_row_count, Slice* cursor);

  static bool ReadHeaderIsNull(Slice *cursor) {
    return cursor->consume_byte() != 0;
  }

  static Result<bool> CheckedReadHeaderIsNull(Slice* cursor) {
    SCHECK_GE(cursor->size(), std::size_t{1}, InvalidArgument, "Unexpected end of data");
    return ReadHeaderIsNull(cursor);
  }
};

}  // namespace pggate
}  // namespace yb
