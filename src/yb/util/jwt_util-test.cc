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

#include <jwt-cpp/jwt.h>

#include <gtest/gtest.h>

#include "yb/util/jwt_test_keys.h"
#include "yb/util/jwt_util.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

namespace yb::util {

#define TEST_JWK_TO_PEM_CONVERSION(algorithm, jwk_json, expected_pem) \
  TEST(JwtCppUtilTest, JWKToPEMConversion##algorithm) { \
    auto jwk = jwt::parse_jwk(testing::jwk_json); \
    auto actual_pem = ASSERT_RESULT(TEST_GetJwkAsPEM(jwk)); \
    ASSERT_EQ(actual_pem, testing::expected_pem); \
  }

TEST_JWK_TO_PEM_CONVERSION(RS256, JWK_RS256, PEM_RS256_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(RS384, JWK_RS384, PEM_RS384_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(RS512, JWK_RS512, PEM_RS512_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(PS256, JWK_PS256, PEM_PS256_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(PS384, JWK_PS384, PEM_PS384_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(PS512, JWK_PS512, PEM_PS512_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(ES256, JWK_ES256, PEM_ES256_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(ES384, JWK_ES384, PEM_ES384_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(ES512, JWK_ES512, PEM_ES512_PUBLIC)
TEST_JWK_TO_PEM_CONVERSION(ES256K, JWK_ES256K, PEM_ES256K_PUBLIC)

}  // namespace yb::util
