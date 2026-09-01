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

#include <regex>
#include <string>

#include <gtest/gtest.h>

#include "yb/util/keyed_fingerprint.h"

namespace yb {

TEST(KeyedFingerprintTest, TokenFormatAndDeterminism) {
  const auto key = GenerateFingerprintKey();
  ASSERT_EQ(32, key.size());

  const auto token = KeyedFingerprintToken(key, "some-value");
  ASSERT_TRUE(std::regex_match(token, std::regex("[0-9a-f]{8}-[0-9a-f]{16}"))) << token;
  // Deterministic under one key: the correlation property.
  ASSERT_EQ(token, KeyedFingerprintToken(key, "some-value"));
}

TEST(KeyedFingerprintTest, TagIdentifiesKeyScope) {
  const auto key = GenerateFingerprintKey();
  const auto token_a = KeyedFingerprintToken(key, "value-a");
  const auto token_b = KeyedFingerprintToken(key, "value-b");
  // Same key: same scope tag, different digests.
  ASSERT_EQ(token_a.substr(0, 8), token_b.substr(0, 8));
  ASSERT_NE(token_a.substr(9), token_b.substr(9));

  // Different key: different scope tag AND a different digest for the same value -- tokens
  // from different scopes are incomparable, and a value cannot be confirmed across scopes.
  const auto other_key = GenerateFingerprintKey();
  ASSERT_NE(key, other_key);
  const auto other_token_a = KeyedFingerprintToken(other_key, "value-a");
  ASSERT_NE(token_a.substr(0, 8), other_token_a.substr(0, 8));
  ASSERT_NE(token_a.substr(9), other_token_a.substr(9));
}

}  // namespace yb
