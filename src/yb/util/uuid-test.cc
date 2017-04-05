// Copyright (c) YugaByte, Inc.

#include "yb/util/uuid.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

class UuidTest : public YBTest {
 protected:
  void RunRoundTrip(const std::string& strval) {
    // Test regular byte conversion.
    Uuid uuid_orig;
    ASSERT_OK(uuid_orig.FromString(strval));
    std::string bytes;
    ASSERT_OK(uuid_orig.ToBytes(&bytes));
    Uuid uuid_new;
    ASSERT_OK(uuid_new.FromBytes(bytes));

    // Test encode and decode.
    std::string encoded_bytes;
    ASSERT_OK(uuid_orig.EncodeToComparable(&encoded_bytes));
    Uuid decoded_uuid_new;
    ASSERT_OK(decoded_uuid_new.DecodeFromComparable(encoded_bytes));

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
  for (const std::string& strval : {
      "123e4567-e89b-02d3-a456-426655440000",
      "123e4567-e89b-12d3-a456-426655440000",
      "123e4567-e89b-22d3-a456-426655440000",
      "123e4567-e89b-32d3-a456-426655440000",
      "123e4567-e89b-42d3-a456-426655440000",
      "11111111-1111-1111-1111-111111111111",
      "00000000-0000-0000-0000-000000000000"}) {
    RunRoundTrip(strval);
  }
}

TEST_F(UuidTest, TestOperators) {
  // Assignment.
  Uuid uuid1;
  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  Uuid uuid2 = uuid1;
  std::string strval;
  ASSERT_OK(uuid2.ToString(&strval));
  ASSERT_EQ("11111111-1111-1111-1111-111111111111", strval);

  // InEquality.
  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("11111111-1111-1111-1111-111111111112"));
  ASSERT_NE(uuid1, uuid2);

  // Comparison.
  // Same type lexical comparison.
  ASSERT_OK(uuid1.FromString("11111111-1111-4111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("21111111-1111-4111-1111-111111111111"));
  ASSERT_LT(uuid1, uuid2);
  ASSERT_LE(uuid1, uuid2);

  // Different type comparison.
  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1111-2111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1111-1211-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1111-1121-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1111-1112-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  // Same type other time comparison.
  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-2111-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1211-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1121-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("01111111-1112-1111-1111-111111111111"));
  LOG(INFO) << (uuid1 < uuid2);
  ASSERT_LT(uuid1, uuid2);
  ASSERT_GT(uuid2, uuid1);

  // Equality comparison
  ASSERT_OK(uuid1.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_OK(uuid2.FromString("11111111-1111-1111-1111-111111111111"));
  ASSERT_LE(uuid1, uuid2);
  ASSERT_GE(uuid1, uuid2);
}

TEST_F(UuidTest, TestErrors) {
  Uuid uuid;
  ASSERT_FALSE(uuid.FromString("11111111-1111-1111-1111-11111111111").ok());
  // Test non-hex UUID.
  ASSERT_FALSE(uuid.FromString("11111111-1111-1111-1111-11111111111X").ok());
  ASSERT_FALSE(uuid.FromString("00000-00-0-0-0-0-0").ok());
  ASSERT_FALSE(uuid.FromString("").ok());

  std::string bytes;
  ASSERT_FALSE(uuid.FromBytes(bytes).ok());
  bytes = "0";
  ASSERT_FALSE(uuid.FromBytes(bytes).ok());
  bytes = "012345";
  ASSERT_FALSE(uuid.FromBytes(bytes).ok());
  bytes = "111111111111111111"; // 17 bytes.
  ASSERT_FALSE(uuid.FromBytes(bytes).ok());
}
} // namespace yb
