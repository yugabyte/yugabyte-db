// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/util/crypt.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class CryptTest: public YBTest {
};

TEST_F(CryptTest, TestHashing) {
  char salt[kBcryptHashSize];
  char hash[kBcryptHashSize];
  int ret;

  const char pass[] = "hi,mom";
  const char hash1[] = "$2a$10$VEVmGHy4F4XQMJ3eOZJAUeb.MedU0W10pTPCuf53eHdKJPiSE8sMK";
  const char hash2[] = "$2a$10$3F0BVk5t8/aoS.3ddaB3l.fxg5qvafQ9NybxcpXLzMeAt.nVWn.NO";

  ret = bcrypt_gensalt(12, salt);
  EXPECT_EQ(0, ret);

  ret = bcrypt_hashpw("testtesttest", salt, hash);
  EXPECT_EQ(0, ret);

  ret = bcrypt_hashpw(pass, hash1, hash);
  EXPECT_EQ(0, ret);
  EXPECT_EQ(*hash1, *hash);

  ret = bcrypt_hashpw(pass, hash2, hash);
  EXPECT_EQ(0, ret);
  EXPECT_EQ(*hash2, *hash);
}

}  // namespace util
}  // namespace yb
