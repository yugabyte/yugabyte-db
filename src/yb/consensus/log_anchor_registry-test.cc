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

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/consensus/log_anchor_registry.h"

#include "yb/util/test_util.h"

using std::string;

using strings::Substitute;

namespace yb {
namespace log {

class LogAnchorRegistryTest : public YBTest {
};

TEST_F(LogAnchorRegistryTest, TestUpdateRegistration) {
  const string test_name = CURRENT_TEST_NAME();
  scoped_refptr<LogAnchorRegistry> reg(new LogAnchorRegistry());

  LogAnchor anchor;
  const int64_t kInitialIndex = 12345;

  ASSERT_FALSE(anchor.is_registered);
  ASSERT_FALSE(anchor.when_registered.Initialized());
  reg->Register(kInitialIndex, test_name, &anchor);
  ASSERT_TRUE(anchor.is_registered);
  ASSERT_TRUE(anchor.when_registered.Initialized());
  ASSERT_OK(reg->UpdateRegistration(kInitialIndex + 1, &anchor));
  ASSERT_OK(reg->Unregister(&anchor));
}

TEST_F(LogAnchorRegistryTest, TestDuplicateInserts) {
  const string test_name = CURRENT_TEST_NAME();
  scoped_refptr<LogAnchorRegistry> reg(new LogAnchorRegistry());

  // Register a bunch of anchors at log index 1.
  constexpr int kNumAnchors = 10;
  LogAnchor anchors[kNumAnchors];
  for (auto& anchor : anchors) {
    reg->Register(1, test_name, &anchor);
  }

  // We should see index 1 as the earliest registered.
  int64_t first_index = -1;
  ASSERT_OK(reg->GetEarliestRegisteredLogIndex(&first_index));
  ASSERT_EQ(1, first_index);

  // Unregister them all.
  for (auto& anchor : anchors) {
    ASSERT_OK(reg->Unregister(&anchor));
  }

  // We should see none registered.
  Status s = reg->GetEarliestRegisteredLogIndex(&first_index);
  ASSERT_TRUE(s.IsNotFound())
      << Substitute("Should have empty OpId registry. Status: $0, anchor: $1, Num anchors: $2",
                    s.ToString(), first_index, reg->GetAnchorCountForTests());

  ASSERT_EQ(0, reg->GetAnchorCountForTests());
}

// Ensure that the correct results are returned when anchors are added/removed
// out of order.
TEST_F(LogAnchorRegistryTest, TestOrderedEarliestOpId) {
  scoped_refptr<LogAnchorRegistry> reg(new LogAnchorRegistry());
  const int kNumAnchors = 4;
  const string test_name = CURRENT_TEST_NAME();

  LogAnchor anchors[kNumAnchors];

  reg->Register(2, test_name, &anchors[0]);
  reg->Register(3, test_name, &anchors[1]);
  reg->Register(1, test_name, &anchors[2]);
  reg->Register(4, test_name, &anchors[3]);

  ASSERT_STR_CONTAINS(reg->DumpAnchorInfo(), "LogAnchor[index=1");

  int64_t anchor_idx = -1;
  ASSERT_OK(reg->GetEarliestRegisteredLogIndex(&anchor_idx));
  ASSERT_EQ(1, anchor_idx);

  ASSERT_OK(reg->Unregister(&anchors[2]));
  ASSERT_OK(reg->GetEarliestRegisteredLogIndex(&anchor_idx));
  ASSERT_EQ(2, anchor_idx);

  ASSERT_OK(reg->Unregister(&anchors[3]));
  ASSERT_OK(reg->GetEarliestRegisteredLogIndex(&anchor_idx));
  ASSERT_EQ(2, anchor_idx);

  ASSERT_OK(reg->Unregister(&anchors[0]));
  ASSERT_OK(reg->GetEarliestRegisteredLogIndex(&anchor_idx));
  ASSERT_EQ(3, anchor_idx);

  ASSERT_OK(reg->Unregister(&anchors[1]));
  Status s = reg->GetEarliestRegisteredLogIndex(&anchor_idx);
  ASSERT_TRUE(s.IsNotFound()) << s.ToString();
}

} // namespace log
} // namespace yb
