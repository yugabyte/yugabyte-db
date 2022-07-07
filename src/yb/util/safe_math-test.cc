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


// Must come before gtest.h.
#include "yb/gutil/mathlimits.h"

#include <gtest/gtest.h>
#include "yb/util/safe_math.h"

namespace yb {
template<typename T>
static void DoTest(T a, T b, bool expected) {
  SCOPED_TRACE(a);
  SCOPED_TRACE(b);
  bool overflow = false;
  T ret = AddWithOverflowCheck(a, b, &overflow);
  EXPECT_EQ(overflow, expected);
  if (!overflow) {
    EXPECT_EQ(ret, a + b);
  }
}

TEST(TestSafeMath, TestSignedInts) {
  // Overflow above max of range.
  DoTest<int32_t>(MathLimits<int32_t>::kMax - 10, 15, true);
  DoTest<int32_t>(MathLimits<int32_t>::kMax - 10, 10, false);

  // Underflow around negative
  DoTest<int32_t>(MathLimits<int32_t>::kMin + 10, -15, true);
  DoTest<int32_t>(MathLimits<int32_t>::kMin + 10, -5, false);
}

TEST(TestSafeMath, TestUnsignedInts) {
  // Overflow above max
  DoTest<uint32_t>(MathLimits<uint32_t>::kMax - 10, 15, true);
  DoTest<uint32_t>(MathLimits<uint32_t>::kMax - 10, 10, false);
}

} // namespace yb
