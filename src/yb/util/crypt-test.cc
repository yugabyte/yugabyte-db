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

#include "yb/util/logging.h"
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
