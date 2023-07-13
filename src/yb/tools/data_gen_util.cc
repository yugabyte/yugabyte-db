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
#include "yb/tools/data_gen_util.h"

#include "yb/client/schema.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/random.h"
#include "yb/util/status.h"

namespace yb {
namespace tools {

void WriteValueToColumn(const client::YBSchema& schema,
                        size_t col_idx,
                        uint64_t value,
                        QLValuePB* out) {
  DataType type = schema.Column(col_idx).type()->main();
  char buf[kFastToBufferSize];
  switch (type) {
    case DataType::INT8:
      out->set_int8_value(static_cast<int32_t>(value));
      return;
    case DataType::INT16:
      out->set_int16_value(static_cast<int32_t>(value));
      return;
    case DataType::INT32:
      out->set_int32_value(static_cast<int32_t>(value));
      return;
    case DataType::INT64:
      out->set_int64_value(value);
      return;
    case DataType::FLOAT:
      out->set_float_value(value / 123.0);
      return;
    case DataType::DOUBLE:
      out->set_double_value(value / 123.0);
      return;
    case DataType::STRING:
      out->set_string_value(FastHex64ToBuffer(value, buf));
      return;
    case DataType::BOOL:
      out->set_bool_value(value);
      return;
    default:
      LOG(FATAL) << "Unexpected data type: " << type;
  }
  FATAL_INVALID_ENUM_VALUE(DataType, type);
}

void GenerateDataForRow(const client::YBSchema& schema, uint64_t record_id,
                        Random* random, QLWriteRequestPB* req) {
  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    // We randomly generate the inserted data, except for the first column,
    // which is always based on a monotonic "record id".
    uint64_t value;
    if (col_idx == 0) {
      value = record_id;
    } else {
      value = random->Next64();
    }
    QLValuePB* out;
    if (col_idx < schema.num_hash_key_columns()) {
      out = req->add_hashed_column_values()->mutable_value();
    } else {
      auto column_value = req->add_column_values();
      column_value->set_column_id(schema.ColumnId(col_idx));
      out = column_value->mutable_expr()->mutable_value();
    }
    WriteValueToColumn(schema, col_idx, value, out);
  }
}

} // namespace tools
} // namespace yb
