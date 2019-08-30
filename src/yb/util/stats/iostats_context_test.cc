// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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

#include "yb/util/stats/iostats_context.h"

namespace yb {

TEST(IOStatsContextTest, ToString) {
  iostats_context.Reset(0);
  iostats_context.bytes_read = 12345;

  std::string zero_included = iostats_context.ToString();
  ASSERT_NE(std::string::npos, zero_included.find("= 0"));
  ASSERT_NE(std::string::npos, zero_included.find("= 12345"));

  std::string zero_excluded = iostats_context.ToString(true);
  ASSERT_EQ(std::string::npos, zero_excluded.find("= 0"));
  ASSERT_NE(std::string::npos, zero_excluded.find("= 12345"));
}

}  // namespace yb
