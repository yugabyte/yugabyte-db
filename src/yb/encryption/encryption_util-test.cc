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

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "yb/encryption/encryption.pb.h"
#include "yb/encryption/encryption_util.h"

#include "yb/util/slice.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb::encryption {

class EncryptionUtilTest : public YBTest {};

// A short key is copied into a heap buffer of exactly its size, so ASAN catches a read past it.
TEST_F(EncryptionUtilTest, FromSliceChecksSize) {
  for (size_t size = 0; size <= 64; ++size) {
    auto key = std::make_unique<char[]>(size);
    auto params = EncryptionParams::FromSlice(Slice(key.get(), size));
    if (size == 32 || size == 40 || size == 48) {
      ASSERT_OK(params);
      ASSERT_EQ(params->get()->key_size, size - EncryptionParams::kUniverseKeyPrefixSize);
    } else {
      ASSERT_NOK(params) << "size " << size;
      ASSERT_TRUE(params.status().IsInvalidArgument()) << params.status();
    }
  }
}

TEST_F(EncryptionUtilTest, FromEncryptionParamsPBChecksSizes) {
  EncryptionParamsPB pb;
  pb.set_nonce(std::string(12, 'n'));
  pb.set_data_key(std::string(32, 'k'));
  ASSERT_OK(EncryptionParams::FromEncryptionParamsPB(pb));

  pb.set_data_key(std::string(64, 'k'));
  ASSERT_NOK(EncryptionParams::FromEncryptionParamsPB(pb));

  pb.set_data_key(std::string(16, 'k'));
  pb.set_nonce(std::string(4, 'n'));
  ASSERT_NOK(EncryptionParams::FromEncryptionParamsPB(pb));
}

} // namespace yb::encryption
