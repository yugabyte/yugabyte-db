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

#include <gtest/gtest.h>

#include "yb/common/types.h"

using std::string;

namespace yb {

TEST(TestTypes, TestTimestampPrinting) {
  const TypeInfo* info = GetTypeInfo(DataType::TIMESTAMP);

  // Test the minimum value
  int64 time;
  info->CopyMinValue(&time);
  string result;
  info->AppendDebugStringForValue(&time, &result);
  ASSERT_EQ("-290308-12-21 19:59:05.224192 GMT", result);
  result = "";

  // Test a regular negative timestamp.
  time = -1454368523123456;
  info->AppendDebugStringForValue(&time, &result);
  ASSERT_EQ("1923-12-01 00:44:36.876544 GMT", result);
  result = "";

  // Test that passing 0 microseconds returns the correct time (0 msecs after the epoch).
  // This is a test for a bug where printing a timestamp with the value 0 would return the
  // current time instead.
  time = 0;
  info->AppendDebugStringForValue(&time, &result);
  ASSERT_EQ("1970-01-01 00:00:00.000000 GMT", result);
  result = "";

  // Test a regular positive timestamp.
  time = 1454368523123456;
  info->AppendDebugStringForValue(&time, &result);
  ASSERT_EQ("2016-02-01 23:15:23.123456 GMT", result);
  result = "";

  // Test the maximum value.
  time = MathLimits<int64>::kMax;
  info->AppendDebugStringForValue(&time, &result);
  ASSERT_EQ("294247-01-10 04:00:54.775807 GMT", result);
}


} // namespace yb
