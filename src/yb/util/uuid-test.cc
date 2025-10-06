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

#include "yb/util/uuid.h"

#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

class UuidTest : public YBTest {
 protected:
  void RunRoundTrip(const std::string& strval) {
    // Test regular byte conversion.
    Uuid uuid_orig = ASSERT_RESULT(Uuid::FromString(strval));
    std::string bytes;
    uuid_orig.ToBytes(&bytes);
    Uuid uuid_new = ASSERT_RESULT(Uuid::FromSlice(bytes));

    // Test encode and decode.
    std::string encoded_bytes;
    uuid_orig.EncodeToComparable(&encoded_bytes);
    Uuid decoded_uuid_new = ASSERT_RESULT(Uuid::FromComparable(encoded_bytes));

    // Test string conversion.
    std::string strval_new;
    ASSERT_OK(uuid_new.ToString(&strval_new));
    std::string decoded_strval_new;
    ASSERT_OK(decoded_uuid_new.ToString(&decoded_strval_new));
    ASSERT_EQ(strval, strval_new);
    ASSERT_EQ(strval, decoded_strval_new);

    // Check the final values.
    ASSERT_EQ(uuid_orig, uuid_new);
    ASSERT_EQ(uuid_orig, decoded_uuid_new);
    LOG(INFO) << "Finished Roundtrip Test for " << strval;
  }
};

TEST_F(UuidTest, TestRoundTrip) {
  // Test all types of UUID.
  for (auto strval : {
      "123e4567-e89b-02d3-a456-426655440000"s,
      "123e4567-e89b-12d3-a456-426655440000"s,
      "123e4567-e89b-22d3-a456-426655440000"s,
      "123e4567-e89b-32d3-a456-426655440000"s,
      "123e4567-e89b-42d3-a456-426655440000"s,
      "11111111-1111-1111-1111-111111111111"s,
      "00000000-0000-0000-0000-000000000000"s}) {
    RunRoundTrip(strval);
  }
}

TEST_F(UuidTest, TestOperators) {
  // Assignment.
  Uuid uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  Uuid uuid2 = uuid1;
  std::string strval;
  ASSERT_OK(uuid2.ToString(&strval));
  ASSERT_EQ("11111111-1111-1111-1111-111111111111", strval);

  // InEquality.
  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111112"));
  ASSERT_NE(uuid1, uuid2);

  // Comparison.
  // Same type lexical comparison.
  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-4111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("21111111-1111-4111-1111-111111111111"));
  ASSERT_LT(uuid1, uuid2);
  ASSERT_LE(uuid1, uuid2);

  // Different type comparison.
  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1111-2111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1111-1211-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1111-1121-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1111-1112-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  // Same type other time comparison.
  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-2111-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1211-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1121-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("01111111-1112-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  // Equality comparison
  uuid1 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  uuid2 = ASSERT_RESULT(Uuid::FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_LE(uuid1, uuid2);
  ASSERT_GE(uuid1, uuid2);
}

TEST_F(UuidTest, TestErrors) {
  ASSERT_FALSE(Uuid::FromString("11111111-1111-1111-1111-11111111111").ok());
  // Test non-hex UUID.
  ASSERT_FALSE(Uuid::FromString("11111111-1111-1111-1111-11111111111X").ok());
  ASSERT_FALSE(Uuid::FromString("00000-00-0-0-0-0-0").ok());
  auto empty_uuid = Uuid::FromString("");
  ASSERT_TRUE(empty_uuid.ok());
  ASSERT_EQ(*empty_uuid, Uuid::Nil());

  ASSERT_NOK(Uuid::FromSlice(""));
  ASSERT_NOK(Uuid::FromSlice("0"));
  ASSERT_NOK(Uuid::FromSlice("012345"));
  ASSERT_NOK(Uuid::FromSlice("111111111111111111")); // 17 bytes.

  // Test hex string
  ASSERT_NOK(Uuid::FromHexString("123"));
  ASSERT_NOK(Uuid::FromHexString("zz111111111111111111111111111111"));
}

Result<Uuid> HexRoundTrip(const std::string& input) {
  auto uuid = VERIFY_RESULT(Uuid::FromHexString(input));
  EXPECT_EQ(input, uuid.ToHexString());
  return uuid;
}

TEST_F(UuidTest, TestHexString) {
  ASSERT_OK(HexRoundTrip("ffffffffffffffffffffffffffffffff"));
  ASSERT_OK(HexRoundTrip("00000000000000000000000000000000"));
  auto uuid = ASSERT_RESULT(HexRoundTrip("11000000000000000000000000000000"));
  EXPECT_EQ("00000000-0000-0000-0000-000000000011", uuid.ToString());
  uuid = ASSERT_RESULT(HexRoundTrip("00004455664256a4d3029be867453e12"));
  EXPECT_EQ("123e4567-e89b-02d3-a456-426655440000", uuid.ToString());
}

} // namespace yb
