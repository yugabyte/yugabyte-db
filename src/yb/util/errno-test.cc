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

#include <string>

#include <gtest/gtest.h>

#include "yb/gutil/macros.h"
#include "yb/util/errno.h"

using std::string;

namespace yb {

TEST(OsUtilTest, TestErrnoToString) {
  int err = ENOENT;

  // Non-truncated result.
  ASSERT_EQ("No such file or directory", ErrnoToString(err));

  // Truncated because of a short buffer.
  char buf[2];
  ErrnoToCString(err, buf, arraysize(buf));
  ASSERT_EQ("N", string(buf));

  // Unknown error.
  string expected = "Unknown error";
  ASSERT_EQ(ErrnoToString(-1).compare(0, expected.length(), expected), 0);

  // Unknown error (truncated).
  ErrnoToCString(-1, buf, arraysize(buf));
  ASSERT_EQ("U", string(buf));
}

} // namespace yb
