// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/row_key-util.h"


#include "yb/gutil/mathlimits.h"

#include "yb/common/common.pb.h"
#include "yb/common/row.h"
#include "yb/common/schema.h"
#include "yb/common/types.h"

#include "yb/util/memory/arena.h"

namespace yb {
namespace row_key_util {

namespace {

template<DataType type>
bool IncrementIntCell(void* cell_ptr) {
  typedef DataTypeTraits<type> traits;
  typedef typename traits::cpp_type cpp_type;

  cpp_type orig;
  memcpy(&orig, cell_ptr, sizeof(cpp_type));

  cpp_type inc;
  if (boost::is_unsigned<cpp_type>::value) {
    inc = orig + 1;
  } else {
    // Signed overflow is undefined in C. So, we'll use a branch here
    // instead of counting on undefined behavior.
    if (orig == MathLimits<cpp_type>::kMax) {
      inc = MathLimits<cpp_type>::kMin;
    } else {
      inc = orig + 1;
    }
  }
  memcpy(cell_ptr, &inc, sizeof(cpp_type));
  return inc > orig;
}

bool IncrementStringCell(void* cell_ptr, Arena* arena) {
  Slice orig;
  memcpy(&orig, cell_ptr, sizeof(orig));
  uint8_t* new_buf = CHECK_NOTNULL(
      static_cast<uint8_t*>(arena->AllocateBytes(orig.size() + 1)));
  memcpy(new_buf, orig.data(), orig.size());
  new_buf[orig.size()] = '\0';

  Slice inc(new_buf, orig.size() + 1);
  memcpy(cell_ptr, &inc, sizeof(inc));
  return true;
}

bool IncrementCell(const ColumnSchema& col, void* cell_ptr, Arena* arena) {
  DataType type = col.type_info()->physical_type();
  switch (type) {
#define HANDLE_TYPE(t) case t: return IncrementIntCell<t>(cell_ptr);
    HANDLE_TYPE(UINT8);
    HANDLE_TYPE(UINT16);
    HANDLE_TYPE(UINT32);
    HANDLE_TYPE(UINT64);
    HANDLE_TYPE(INT8);
    HANDLE_TYPE(INT16);
    HANDLE_TYPE(INT32);
    HANDLE_TYPE(TIMESTAMP);
    HANDLE_TYPE(INT64);
    case UNKNOWN_DATA:
    case BOOL:
    case FLOAT:
    case DOUBLE:
      LOG(FATAL) << "Unable to handle type " << type << " in row keys";
    case STRING:
    case BINARY:
      return IncrementStringCell(cell_ptr, arena);
    default: CHECK(false) << "Unknown data type: " << type;
  }
  return false; // unreachable
#undef HANDLE_TYPE
}

} // anonymous namespace

void SetKeyToMinValues(ContiguousRow* row) {
  for (int i = 0; i < row->schema()->num_key_columns(); i++) {
    const ColumnSchema& col = row->schema()->column(i);
    col.type_info()->CopyMinValue(row->mutable_cell_ptr(i));
  }
}

bool IncrementKey(ContiguousRow* row, Arena* arena) {
  return IncrementKeyPrefix(row, row->schema()->num_key_columns(), arena);
}

bool IncrementKeyPrefix(ContiguousRow* row, size_t prefix_len, Arena* arena) {
  for (size_t i = prefix_len; i > 0;) {
    --i;
    if (IncrementCell(row->schema()->column(i), row->mutable_cell_ptr(i), arena)) {
      return true;
    }
  }
  return false;
}

} // namespace row_key_util
} // namespace yb
