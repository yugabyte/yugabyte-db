// Copyright (c) YugaByte, Inc.
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

#include "yb/util/strongly_typed_uuid.h"

#include <gtest/gtest.h>

namespace yb {
namespace util {

YB_STRONGLY_TYPED_UUID(TestUuid);

TEST(TestStronglyTypedUuid, TestBasic) {
  // Assert that constant kUndefined.IsValid() is false and that undefined == undefined.
  ASSERT_FALSE(TestUuid::kUndefined.IsValid());
  ASSERT_TRUE(TestUuid::kUndefined == TestUuid::kUndefined);
  ASSERT_EQ(TestUuid::kUndefined, TestUuid::kUndefined);

  TestUuid strongly_typed_uuid_0 = TestUuid::GenerateRandomUuid();
  TestUuid strongly_typed_uuid_0_copy = strongly_typed_uuid_0;
  TestUuid strongly_typed_uuid_1 = TestUuid::GenerateRandomUuid();

  // Assert two strongly typed uuids created from the same uuid are equal.
  ASSERT_TRUE(strongly_typed_uuid_0 == strongly_typed_uuid_0_copy);
  ASSERT_EQ(strongly_typed_uuid_0, strongly_typed_uuid_0_copy);

  // Assert two strongly typed uuids created from different uuids are not equal.
  ASSERT_TRUE(strongly_typed_uuid_0 != strongly_typed_uuid_1);
  ASSERT_NE(strongly_typed_uuid_0, strongly_typed_uuid_1);

  // Assert that GenerateUuidFromString and ToString are inverses.
  auto strong_typed_uuid_0_from_string =
      TestUuid::GenerateUuidFromString(strongly_typed_uuid_0.ToString());
  ASSERT_TRUE(strong_typed_uuid_0_from_string.ok());
  ASSERT_EQ(strongly_typed_uuid_0, *strong_typed_uuid_0_from_string);

  // Assert that generating a uuid from "" is undefined.
  auto uuid_from_string_empty = TestUuid::GenerateUuidFromString("");
  ASSERT_TRUE(uuid_from_string_empty.ok());
  ASSERT_FALSE((*uuid_from_string_empty).IsValid());

  // Assert that uuid from invalid string returns result not okay.
  auto uuid_from_string_invalid = TestUuid::GenerateUuidFromString("invalid_string");
  ASSERT_FALSE(uuid_from_string_invalid.ok());

  // Assert that ToString of undefined uuid returns "<Uuid undefined>".
  ASSERT_EQ(TestUuid::kUndefined.ToString(), "<UndefinedTestUuid>");

  // Assert that * operator and constructor are inverses.
  ASSERT_EQ(strongly_typed_uuid_0, TestUuid(*strongly_typed_uuid_0));

  // Assert that a defined uuid is valid.
  ASSERT_TRUE(strongly_typed_uuid_0.IsValid());
}

} // namespace util
} // namespace yb

