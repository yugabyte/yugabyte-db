// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/enums.h"

#include "yb/util/test_util.h"

namespace yb {

class EnumsTest : public YBTest {
};

YB_DEFINE_ENUM(
    TestEnum,
    (kElement1)
    (kItem2)
    (kWidget3))

TEST_F(EnumsTest, FromStream) {
  std::vector<std::pair<std::string, TestEnum>> test_cases{
    {"kelement1", TestEnum::kElement1},
    {"keleMent1", TestEnum::kElement1},
    {"Element1", TestEnum::kElement1},
    {"kitem2", TestEnum::kItem2},
    {"kItEm2", TestEnum::kItem2},
    {"IteM2", TestEnum::kItem2},
    {"kwidGet3", TestEnum::kWidget3},
    {"kwiDget3", TestEnum::kWidget3},
    {"Widget3", TestEnum::kWidget3},
  };

  for (const auto& [input_str, expected_enum] : test_cases) {
    SCOPED_TRACE("input_str: " + input_str);
    std::istringstream input_stream(input_str);
    TestEnum value;
    input_stream >> value;
    ASSERT_FALSE(input_stream.fail());
    ASSERT_EQ(value, expected_enum);
  }

  std::istringstream invalid_input1("foo");
  TestEnum value5;
  invalid_input1 >> value5;
  ASSERT_TRUE(invalid_input1.fail());
}

}  // namespace yb
