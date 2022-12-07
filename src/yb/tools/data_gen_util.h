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
// Utility functions for generating data for use by tools and tests.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace yb {

class QLValuePB;
class QLWriteRequestPB;
class Random;

namespace client {
class YBSchema;
} // namespace client

namespace tools {

// Detect the type of the given column and coerce the given number value in
// 'value' to the data type of that column.
// At the time of this writing, we only support ints, bools, and strings.
// For the numbers / bool, the value is truncated to fit the data type.
// For the string, we encode the number as hex.
void WriteValueToColumn(const client::YBSchema& schema,
                        size_t col_idx,
                        uint64_t value,
                        QLValuePB* out);

// Generate row data for an arbitrary schema. Initial column value determined
// by the value of 'record_id'.
void GenerateDataForRow(const client::YBSchema& schema, uint64_t record_id,
                        Random* random, QLWriteRequestPB* req);

} // namespace tools
} // namespace yb
