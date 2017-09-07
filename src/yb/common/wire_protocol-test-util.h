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

#ifndef YB_COMMON_WIRE_PROTOCOL_TEST_UTIL_H_
#define YB_COMMON_WIRE_PROTOCOL_TEST_UTIL_H_

#include "yb/common/wire_protocol.h"

#include <string>

#include "yb/common/partial_row.h"
#include "yb/common/row.h"
#include "yb/common/row_operations.h"

namespace yb {

inline Schema GetSimpleTestSchema() {
  return Schema({ ColumnSchema("key", INT32),
                  ColumnSchema("int_val", INT32),
                  ColumnSchema("string_val", STRING, true) },
                1);
}

inline void AddTestRowWithNullableStringToPB(RowOperationsPB::Type op_type,
                                             const Schema& schema,
                                             int32_t key,
                                             int32_t int_val,
                                             const char* string_val,
                                             RowOperationsPB* ops) {
  DCHECK(schema.initialized());
  YBPartialRow row(&schema);
  CHECK_OK(row.SetInt32("key", key));
  CHECK_OK(row.SetInt32("int_val", int_val));
  if (string_val) {
    CHECK_OK(row.SetStringCopy("string_val", string_val));
  }
  RowOperationsPBEncoder enc(ops);
  enc.Add(op_type, row);
}

inline void AddTestRowToPB(RowOperationsPB::Type op_type,
                           const Schema& schema,
                           int32_t key,
                           int32_t int_val,
                           const string& string_val,
                           RowOperationsPB* ops) {
  AddTestRowWithNullableStringToPB(op_type, schema, key, int_val, string_val.c_str(), ops);
}

inline void AddTestKeyToPB(RowOperationsPB::Type op_type,
                    const Schema& schema,
                    int32_t key,
                    RowOperationsPB* ops) {
  YBPartialRow row(&schema);
  CHECK_OK(row.SetInt32(0, key));
  RowOperationsPBEncoder enc(ops);
  enc.Add(op_type, row);
}

} // namespace yb

#endif /* YB_COMMON_WIRE_PROTOCOL_TEST_UTIL_H_ */
