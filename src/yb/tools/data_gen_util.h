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
// Utility functions for generating data for use by tools and tests.

#ifndef KUDU_TOOLS_DATA_GEN_UTIL_H_
#define KUDU_TOOLS_DATA_GEN_UTIL_H_

#include <stdint.h>

namespace kudu {
class KuduPartialRow;
class Random;

namespace client {
class KuduSchema;
} // namespace client

namespace tools {

// Detect the type of the given column and coerce the given number value in
// 'value' to the data type of that column.
// At the time of this writing, we only support ints, bools, and strings.
// For the numbers / bool, the value is truncated to fit the data type.
// For the string, we encode the number as hex.
void WriteValueToColumn(const client::KuduSchema& schema,
                        int col_idx,
                        uint64_t value,
                        KuduPartialRow* row);

// Generate row data for an arbitrary schema. Initial column value determined
// by the value of 'record_id'.
void GenerateDataForRow(const client::KuduSchema& schema, uint64_t record_id,
                        Random* random, KuduPartialRow* row);

} // namespace tools
} // namespace kudu

#endif // KUDU_TOOLS_DATA_GEN_UTIL_H_
