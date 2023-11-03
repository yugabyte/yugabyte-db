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
#include <gtest/gtest.h>

#include "yb/util/result.h"
#include "yb/util/strongly_typed_uuid.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace util {

YB_STRONGLY_TYPED_UUID(TestUuid);

TEST(TestStronglyTypedUuid, TestBasic) {
  // Assert that constant kUndefined.IsValid() is false and that undefined == undefined.
  ASSERT_TRUE(TestUuid::Nil().IsNil());
  ASSERT_TRUE(TestUuid::Nil() == TestUuid::Nil());
  ASSERT_EQ(TestUuid::Nil(), TestUuid::Nil());

  TestUuid strongly_typed_uuid_0 = TestUuid::GenerateRandom();
  TestUuid strongly_typed_uuid_0_copy = strongly_typed_uuid_0;
  TestUuid strongly_typed_uuid_1 = TestUuid::GenerateRandom();

  // Assert two strongly typed uuids created from the same uuid are equal.
  ASSERT_TRUE(strongly_typed_uuid_0 == strongly_typed_uuid_0_copy);
  ASSERT_EQ(strongly_typed_uuid_0, strongly_typed_uuid_0_copy);

  // Assert two strongly typed uuids created from different uuids are not equal.
  ASSERT_TRUE(strongly_typed_uuid_0 != strongly_typed_uuid_1);
  ASSERT_NE(strongly_typed_uuid_0, strongly_typed_uuid_1);

  // Assert that GenerateUuidFromString and ToString are inverses.
  auto strong_typed_uuid_0_from_string =
      TestUuidFromString(strongly_typed_uuid_0.ToString());
  ASSERT_TRUE(strong_typed_uuid_0_from_string.ok());
  ASSERT_EQ(strongly_typed_uuid_0, *strong_typed_uuid_0_from_string);

  // Assert that generating a uuid from "" is undefined.
  auto uuid_from_string_empty = ASSERT_RESULT(TestUuidFromString(""));
  ASSERT_TRUE(uuid_from_string_empty.IsNil());

  // Assert that uuid from invalid string returns result not okay.
  auto uuid_from_string_invalid = TestUuidFromString("invalid_string");
  LOG(INFO) << "uuid_from_string_invalid: " << uuid_from_string_invalid;
  ASSERT_FALSE(uuid_from_string_invalid.ok());

  // Assert that ToString of undefined uuid returns "<Uuid undefined>".
  ASSERT_EQ(TestUuid::Nil().ToString(), "00000000-0000-0000-0000-000000000000");

  // Assert that * operator and constructor are inverses.
  ASSERT_EQ(strongly_typed_uuid_0, TestUuid(Uuid(*strongly_typed_uuid_0)));

  // Assert that a defined uuid is valid.
  ASSERT_FALSE(strongly_typed_uuid_0.IsNil());
}

} // namespace util
} // namespace yb
