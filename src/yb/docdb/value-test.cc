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

#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/util/random.h"
#include "yb/util/test_util.h"
#include "yb/util/uuid.h"

namespace yb {
namespace docdb {

class ValueTest : public YBTest {
};

TEST_F(ValueTest, TestEncodeDecode) {
  Random r(0);
  PrimitiveValue primitive_value(r.Next64());
  MonoDelta ttl = MonoDelta::FromSeconds(r.Next32());
  int64_t timestamp = r.Next64();
  Value value(primitive_value, ttl, timestamp);
  LOG(INFO) << "Testing Value: " << value.ToString();

  std::string value_bytes = value.Encode();
  Value decoded_value;
  // Test values without decoding.
  ASSERT_TRUE(decoded_value.ttl().Equals(Value::kMaxTtl));
  ASSERT_EQ(Value::kInvalidUserTimestamp, decoded_value.user_timestamp());

  // Now decode and test.
  ASSERT_OK(decoded_value.Decode(value_bytes));
  ASSERT_EQ(primitive_value, decoded_value.primitive_value());
  ASSERT_TRUE(ttl.Equals(decoded_value.ttl()));
  ASSERT_EQ(timestamp, decoded_value.user_timestamp());

  // Test decode value type.
  ValueType value_type;
  ASSERT_OK(Value::DecodePrimitiveValueType(value_bytes, &value_type));
  ASSERT_EQ(ValueType::kInt64, value_type);

  // Test decode value type without ttl and timestamp.
  Value val1(PrimitiveValue(r.Next64()));
  value_bytes = val1.Encode();
  ASSERT_OK(Value::DecodePrimitiveValueType(value_bytes, &value_type));
  ASSERT_EQ(ValueType::kInt64, value_type);
}

}  // namespace docdb
}  // namespace yb
