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

#include <unordered_set>

#include <gtest/gtest.h>

#include "yb/gutil/map-util.h"
#include "yb/util/flags.h"
#include "yb/util/test_util.h"

DEFINE_NON_RUNTIME_int32(flag_with_no_tags, 0, "test flag that has no tags");

DEFINE_NON_RUNTIME_int32(flag_with_one_tag, 0, "test flag that has 1 tag");
TAG_FLAG(flag_with_one_tag, stable);

DEFINE_NON_RUNTIME_int32(flag_with_two_tags, 0, "test flag that has 2 tags");
TAG_FLAG(flag_with_two_tags, evolving);
TAG_FLAG(flag_with_two_tags, unsafe);

DEFINE_NON_RUNTIME_bool(non_runtime_bool, false, "test flag");
DEFINE_NON_RUNTIME_int32(non_runtime_int32, 0, "test flag");
DEFINE_NON_RUNTIME_uint32(non_runtime_uint32, 0, "test flag");
DEFINE_NON_RUNTIME_int64(non_runtime_int64, 0, "test flag");
DEFINE_NON_RUNTIME_uint64(non_runtime_uint64, 0, "test flag");
DEFINE_NON_RUNTIME_double(non_runtime_double, 0, "test flag");
DEFINE_NON_RUNTIME_string(non_runtime_string, "0", "test flag");

DEFINE_RUNTIME_bool(runtime_bool, false, "test flag");
DEFINE_RUNTIME_int32(runtime_int32, 0, "test flag");
DEFINE_RUNTIME_uint32(runtime_uint32, 0, "test flag");
DEFINE_RUNTIME_int64(runtime_int64, 0, "test flag");
DEFINE_RUNTIME_uint64(runtime_uint64, 0, "test flag");
DEFINE_RUNTIME_double(runtime_double, 0, "test flag");
DEFINE_RUNTIME_string(runtime_string, "0", "test flag");

DEFINE_UNKNOWN_bool(unknown_bool, false, "test flag");
DEFINE_UNKNOWN_int32(unknown_int32, 0, "test flag");
DEFINE_UNKNOWN_uint32(unknown_uint32, 0, "test flag");
DEFINE_UNKNOWN_int64(unknown_int64, 0, "test flag");
DEFINE_UNKNOWN_uint64(unknown_uint64, 0, "test flag");
DEFINE_UNKNOWN_double(unknown_double, 0, "test flag");
DEFINE_UNKNOWN_string(unknown_string, "0", "test flag");

using std::string;
using std::unordered_set;

namespace yb {

class FlagTagsTest : public YBTest {};

TEST_F(FlagTagsTest, TestTags) {
  unordered_set<FlagTag> tags;
  GetFlagTags("flag_with_no_tags", &tags);
  ASSERT_EQ(0, tags.size());

  GetFlagTags("flag_with_one_tag", &tags);
  ASSERT_EQ(1, tags.size());
  ASSERT_TRUE(ContainsKey(tags, FlagTag::kStable));

  GetFlagTags("flag_with_two_tags", &tags);
  ASSERT_EQ(2, tags.size());
  ASSERT_TRUE(ContainsKey(tags, FlagTag::kEvolving));
  ASSERT_TRUE(ContainsKey(tags, FlagTag::kUnsafe));

  GetFlagTags("missing_flag", &tags);
  ASSERT_EQ(0, tags.size());
}

TEST_F(FlagTagsTest, TestRuntimeTags) {
  unordered_set<FlagTag> tags;

  // Test runtime flags.
  GetFlagTags("runtime_bool", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("runtime_int32", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("runtime_uint32", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("runtime_int64", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("runtime_uint64", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("runtime_double", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("runtime_string", &tags);
  ASSERT_TRUE(tags.contains(FlagTag::kRuntime));

  // Test non runtime flags.
  GetFlagTags("non_runtime_bool", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("non_runtime_int32", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("non_runtime_uint32", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("non_runtime_int64", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("non_runtime_uint64", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("non_runtime_double", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("non_runtime_string", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));

  // Test unknown flags.
  GetFlagTags("unknown_bool", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("unknown_int32", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("unknown_uint32", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("unknown_int64", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("unknown_uint64", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("unknown_double", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
  GetFlagTags("unknown_string", &tags);
  ASSERT_FALSE(tags.contains(FlagTag::kRuntime));
}

}  // namespace yb
