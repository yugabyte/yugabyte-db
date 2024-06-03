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

#include "yb/util/jwtcpp_util.h"
#include "yb/util/jwt_test_keys.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

namespace yb::util {

Result<jwt::builder<jwt::traits::kazuho_picojson>> GenerateBuilder(std::string alg_prefix) {
  try {
    auto builder =
        jwt::create().set_issuer(testing::ISSUER)
                     .set_audience(testing::AUDIENCE)
                     .set_type("JWT")
                     .set_key_id(Format("$0_keyid", alg_prefix))
                     .set_issued_at(std::chrono::system_clock::now())
                     .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{36000})
                     .set_subject(testing::SUBJECT);
    return builder;
  } catch (...) {
    return STATUS(InvalidArgument, "Could not create builder");
  }
}

#define JWT_ALGORITHM(alg, private_key) jwt::algorithm::alg("", private_key)

TEST(JwtCppUtilTest, ParseJwksSuccess) {
  ASSERT_OK(ParseJwks(testing::JWKS));
}

TEST(JwtCppUtilTest, ParseJwksInvalid) {
  ASSERT_NOK(ParseJwks("illformatted_json"));
}

TEST(JwtCppUtilTest, GetJwkFromJwksSuccess) {
  auto jwks = ASSERT_RESULT(ParseJwks(testing::JWKS));
  auto jwk = ASSERT_RESULT(GetJwkFromJwks(jwks, "ps512_keyid"));
  ASSERT_EQ(ASSERT_RESULT(GetJwkKeyId(jwk)), "ps512_keyid");
}

TEST(JwtCppUtilTest, GetJwkFromJwksInvalid) {
  auto jwks = ASSERT_RESULT(ParseJwks(testing::JWKS));
  ASSERT_NOK(GetJwkFromJwks(jwks, "missing_keyid"));
}

TEST(JwtCppUtilTest, GetX5cKeyValueFromJwkSuccess) {
  auto jwks_with_x5c = R"(
        {
            "keys": [
                {
                    "kty": "EC",
                    "d": "N8JUNzTt-alwdQjCCDzdoGMebq1OaQl7OZpsUbx0x9Y",
                    "crv": "secp256k1",
                    "kid": "ps512_keyid",
                    "x": "IMlyAhf2nqY1gTtVuQhHjr8sJGTR9UAlqEyIApUNZKw",
                    "y": "Rv32cdCqWoDhcWE-fep8xMg2SR3hqi4Ter0J9Jk_Vb8",
                    "alg": "ES256K",
                    "x5c": [
                        "some x5c value"
                    ]
                }
            ]
        }
    )";
  auto jwks = ASSERT_RESULT(ParseJwks(jwks_with_x5c));
  auto jwk = ASSERT_RESULT(GetJwkFromJwks(jwks, "ps512_keyid"));
  ASSERT_EQ(ASSERT_RESULT(GetX5cKeyValueFromJwk(jwk)), "some x5c value");
}

TEST(JwtCppUtilTest, GetX5cKeyValueFromJwkInvalid) {
  auto jwks_without_x5c = R"(
        {
            "keys": [
                {
                    "kty": "EC",
                    "d": "N8JUNzTt-alwdQjCCDzdoGMebq1OaQl7OZpsUbx0x9Y",
                    "crv": "secp256k1",
                    "kid": "ps512_keyid",
                    "x": "IMlyAhf2nqY1gTtVuQhHjr8sJGTR9UAlqEyIApUNZKw",
                    "y": "Rv32cdCqWoDhcWE-fep8xMg2SR3hqi4Ter0J9Jk_Vb8",
                    "alg": "ES256K",
                    "bool_field": true
                }
            ]
        }
    )";
  auto jwks = ASSERT_RESULT(ParseJwks(jwks_without_x5c));
  auto jwk = ASSERT_RESULT(GetJwkFromJwks(jwks, "ps512_keyid"));
  ASSERT_NOK(GetX5cKeyValueFromJwk(jwk));
}

TEST(JwtCppUtilTest, GetJwkClaimAsStringSuccess) {
  auto jwks = ASSERT_RESULT(ParseJwks(testing::JWKS));
  auto jwk = ASSERT_RESULT(GetJwkFromJwks(jwks, "ps512_keyid"));
  ASSERT_EQ(ASSERT_RESULT(GetJwkClaimAsString(jwk, "kid")), "ps512_keyid");
}

TEST(JwtCppUtilTest, GetJwkClaimAsStringInvalidBool) {
  auto jwks_with_bool_field = R"(
        {
            "keys": [
                {
                    "kty": "EC",
                    "d": "N8JUNzTt-alwdQjCCDzdoGMebq1OaQl7OZpsUbx0x9Y",
                    "crv": "secp256k1",
                    "kid": "ps512_keyid",
                    "x": "IMlyAhf2nqY1gTtVuQhHjr8sJGTR9UAlqEyIApUNZKw",
                    "y": "Rv32cdCqWoDhcWE-fep8xMg2SR3hqi4Ter0J9Jk_Vb8",
                    "alg": "ES256K",
                    "bool_field": true
                }
            ]
        }
    )";
  auto jwks = ASSERT_RESULT(ParseJwks(jwks_with_bool_field));
  auto jwk = ASSERT_RESULT(GetJwkFromJwks(jwks, "ps512_keyid"));
  // The claim value was expected to be a string but is a bool.
  ASSERT_NOK(GetJwkClaimAsString(jwk, "bool_field"));
}

TEST(JwtCppUtilTest, ConvertX5cDerToPemSuccess) {
  auto der_encoded_x5c =
      "MIIDBTCCAe2gAwIBAgIQH4FlYNA+UJlF0G3vy9ZrhTANBgkqhkiG9w0BAQsFADAtMSswKQYDVQQDEyJhY2NvdW50cy5h"
      "Y2Nlc3Njb250cm9sLndpbmRvd3MubmV0MB4XDTIyMDUyMjIwMDI0OVoXDTI3MDUyMjIwMDI0OVowLTErMCkGA1UEAxMi"
      "YWNjb3VudHMuYWNjZXNzY29udHJvbC53aW5kb3dzLm5ldDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMBD"
      "DCbY/cjEHfEEulZ5ud/CuRjdT6/yN9fy1JffjgmLvvfw6w7zxo1YkCvZDogowX8qqAC/qQXnJ/fl12kvguMWU59WUcPv"
      "hhC2m7qNLvlOq90yo+NsRQxD/v0eUaThrIaAveZayolObXroZ+HwTN130dhgdHVTHKczd4ePtDjLwSv/2a/bZEAlPys1"
      "02zQo8gO8m7W6/NzRfZNyo6U8jsmNkvqrxW2PgKKjIS/UafK9hwY/767K+kV+hnokscY2xMwxQNlSHEim0h72zQRHlti"
      "oy15M+kBti4ys+V7GC6epL//pPZT0Acv1ewouGZIQDfuo9UtSnKufGi26dMAzSkCAwEAAaMhMB8wHQYDVR0OBBYEFLFr"
      "+sjUQ+IdzGh3eaDkzue2qkTZMA0GCSqGSIb3DQEBCwUAA4IBAQCiVN2A6ErzBinGYafC7vFv5u1QD6nbvY32A8KycJwK"
      "Wy1sa83CbLFbFi92SGkKyPZqMzVyQcF5aaRZpkPGqjhzM+iEfsR2RIf+/noZBlR/esINfBhk4oBruj7SY+kPjYzV03Ne"
      "Y0cfO4JEf6kXpCqRCgp9VDRM44GD8mUV/ooN+XZVFIWs5Gai8FGZX9H8ZSgkIKbxMbVOhisMqNhhp5U3fT7VPsl94ril"
      "J8gKXP/KBbpldrfmOAdVDgUC+MHw3sSXSt+VnorB4DU4mUQLcMriQmbXdQc8d1HUZYZEkcKaSgbygHLtByOJF44XUsBo"
      "tsTfZ4i/zVjnYcjgUQmwmAWD";
  // The x5c is just enclosed with "BEGIN CERTIFICATE" and "END CERTIFICATE" lines.
  auto expected_pem = R"(-----BEGIN CERTIFICATE-----
MIIDBTCCAe2gAwIBAgIQH4FlYNA+UJlF0G3vy9ZrhTANBgkqhkiG9w0BAQsFADAt
MSswKQYDVQQDEyJhY2NvdW50cy5hY2Nlc3Njb250cm9sLndpbmRvd3MubmV0MB4X
DTIyMDUyMjIwMDI0OVoXDTI3MDUyMjIwMDI0OVowLTErMCkGA1UEAxMiYWNjb3Vu
dHMuYWNjZXNzY29udHJvbC53aW5kb3dzLm5ldDCCASIwDQYJKoZIhvcNAQEBBQAD
ggEPADCCAQoCggEBAMBDDCbY/cjEHfEEulZ5ud/CuRjdT6/yN9fy1JffjgmLvvfw
6w7zxo1YkCvZDogowX8qqAC/qQXnJ/fl12kvguMWU59WUcPvhhC2m7qNLvlOq90y
o+NsRQxD/v0eUaThrIaAveZayolObXroZ+HwTN130dhgdHVTHKczd4ePtDjLwSv/
2a/bZEAlPys102zQo8gO8m7W6/NzRfZNyo6U8jsmNkvqrxW2PgKKjIS/UafK9hwY
/767K+kV+hnokscY2xMwxQNlSHEim0h72zQRHltioy15M+kBti4ys+V7GC6epL//
pPZT0Acv1ewouGZIQDfuo9UtSnKufGi26dMAzSkCAwEAAaMhMB8wHQYDVR0OBBYE
FLFr+sjUQ+IdzGh3eaDkzue2qkTZMA0GCSqGSIb3DQEBCwUAA4IBAQCiVN2A6Erz
BinGYafC7vFv5u1QD6nbvY32A8KycJwKWy1sa83CbLFbFi92SGkKyPZqMzVyQcF5
aaRZpkPGqjhzM+iEfsR2RIf+/noZBlR/esINfBhk4oBruj7SY+kPjYzV03NeY0cf
O4JEf6kXpCqRCgp9VDRM44GD8mUV/ooN+XZVFIWs5Gai8FGZX9H8ZSgkIKbxMbVO
hisMqNhhp5U3fT7VPsl94rilJ8gKXP/KBbpldrfmOAdVDgUC+MHw3sSXSt+VnorB
4DU4mUQLcMriQmbXdQc8d1HUZYZEkcKaSgbygHLtByOJF44XUsBotsTfZ4i/zVjn
YcjgUQmwmAWD
-----END CERTIFICATE-----
)";
  ASSERT_EQ(ASSERT_RESULT(ConvertX5cDerToPem(der_encoded_x5c)), expected_pem);
}

TEST(JwtCppUtilTest, ConvertX5cDerToPemInvalid) {
  ASSERT_NOK(ConvertX5cDerToPem("an invalid x5c"));
}

TEST(JwtCppUtilTest, DecodeJwtSuccess) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));

  ASSERT_EQ(decoded_jwt.get_key_id(), "rs256_keyid");
  ASSERT_EQ(decoded_jwt.get_subject(), testing::SUBJECT);
}

TEST(JwtCppUtilTest, DecodeJwtInvalid) {
  ASSERT_NOK(DecodeJwt("an_invalid_jwt"));
}

TEST(JwtCppUtilTest, GetJwtKeyIdSuccess) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));
  auto key_id = ASSERT_RESULT(GetJwtKeyId(decoded_jwt));

  ASSERT_EQ(key_id, "rs256_keyid");
}

TEST(JwtCppUtilTest, GetJwtKeyIdInvalid) {
  // It is not possible to generate a JWT without key id via the library, so this is hardcoded
  // after generating online.
  auto jwt_without_key_id =
      "eyJhbGciOiJSUzI1NiIsImN0eSI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwiaWF0IjoxNjAzMzc2MDExfQ.EzWU"
      "2fTxcQbOvkK-SkRyEJTFjRboB0gIdhXjisfrTxd76UewpsNz81wMNeweBKZ1pkUFM1hFsvupO5TOf_yS7NjaMH649uQx"
      "G-i2EZR4H_sbXZ-b7afYPMbmjJg80sxH4C4HLavCi-3PEVajuEHAPFAS1jiRPtMqHMDPtOIymJenhdZfReSTsyCPQAqP"
      "n-Y4uC-7cbHeT619bb1dvbooOb24VuH7sk1SwZR_ITmQJzx-95QrYOA4AnctmdlJ3Ez_-5Ti3WWfIKjOm5QpsbOlILqu"
      "AJ-q-11scFTwcSbopcz8MkhmX76tJawgIPp2_oVjwSSDTUH1WvFPfuN17gSZVw";
  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt_without_key_id));

  ASSERT_NOK(GetJwtKeyId(decoded_jwt));
}

TEST(JwtCppUtilTest, GetJwtIssuerSuccess) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));
  auto issuer = ASSERT_RESULT(GetJwtIssuer(decoded_jwt));

  ASSERT_EQ(issuer, testing::ISSUER);
}

TEST(JwtCppUtilTest, GetJwtIssuerInvalid) {
  auto builder_without_issuer =
      jwt::create().set_audience(testing::AUDIENCE)
                   .set_type("JWT")
                   .set_key_id(Format("rs256_keyid"))
                   .set_issued_at(std::chrono::system_clock::now())
                   .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{36000})
                   .set_subject(testing::SUBJECT);
  auto jwt = builder_without_issuer.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));
  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));

  auto issuer_result = GetJwtIssuer(decoded_jwt);
  ASSERT_NOK(issuer_result);
  ASSERT_NE(
      issuer_result.status().message().ToBuffer().find("GetJwtIssuer failed: claim not found"),
      std::string::npos)
      << issuer_result.status();
}

TEST(JwtCppUtilTest, GetJwtClaimAsStringsListSuccess) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  builder.set_payload_claim(testing::CUSTOM_KEY, jwt::claim(picojson::value("abc")));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));

  auto claim_value = ASSERT_RESULT(GetJwtClaimAsStringsList(decoded_jwt, testing::CUSTOM_KEY));
  auto expected_value = std::vector<std::string>{"abc"};
  ASSERT_EQ(claim_value, expected_value);
}

void TestGetJwtClaimAsStringsListInvalid(const std::string& jwt) {
  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));

  auto get_claim_result = GetJwtClaimAsStringsList(decoded_jwt, testing::CUSTOM_KEY);
  ASSERT_NOK(get_claim_result);
  ASSERT_NE(
      get_claim_result.status().message().ToBuffer().find(
          "Claim value with name customkey was not a string or array of string."),
      std::string::npos)
      << get_claim_result.status();
}

// Claim value is not an array.
TEST(JwtCppUtilTest, GetJwtClaimAsStringsListInvalidPrimitive) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  builder.set_payload_claim(testing::CUSTOM_KEY, jwt::claim(picojson::value(int64_t{12345})));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  TestGetJwtClaimAsStringsListInvalid(jwt);
}

// Claim value is an array whose inner type is not a string.
TEST(JwtCppUtilTest, GetJwtClaimAsStringsListInvalidArray) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  std::vector<picojson::value> claim_value = {
      picojson::value(int64_t{1}), picojson::value(int64_t{2})};
  builder.set_payload_claim(testing::CUSTOM_KEY, jwt::claim(picojson::value(claim_value)));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  TestGetJwtClaimAsStringsListInvalid(jwt);
}

TEST(JwtCppUtilTest, GetJwtAudiencesSingletonSuccess) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));
  auto audiences = ASSERT_RESULT(GetJwtAudiences(decoded_jwt));

  ASSERT_EQ(audiences, std::set<std::string>{testing::AUDIENCE});
}

TEST(JwtCppUtilTest, GetJwtAudiencesMultipleSuccess) {
  auto builder = ASSERT_RESULT(GenerateBuilder("rs256"));
  builder.set_audience(std::vector<picojson::value>{
      picojson::value(testing::AUDIENCE), picojson::value("ANOTHER_AUDIENCE")});
  auto jwt = builder.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));

  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));
  auto audiences = ASSERT_RESULT(GetJwtAudiences(decoded_jwt));

  auto expected_audience_set = std::set<std::string>{testing::AUDIENCE, "ANOTHER_AUDIENCE"};
  ASSERT_EQ(audiences, expected_audience_set);
}

TEST(JwtCppUtilTest, GetJwtAudiencesInvalid) {
  auto builder_without_audiences =
      jwt::create().set_issuer(testing::ISSUER)
                   .set_type("JWT")
                   .set_key_id(Format("rs256_keyid"))
                   .set_issued_at(std::chrono::system_clock::now())
                   .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{36000})
                   .set_subject(testing::SUBJECT);
  auto jwt = builder_without_audiences.sign(JWT_ALGORITHM(rs256, testing::PEM_RS256_PRIVATE));
  auto decoded_jwt = ASSERT_RESULT(DecodeJwt(jwt));

  auto audiences_res = GetJwtAudiences(decoded_jwt);
  ASSERT_NOK(audiences_res);
  ASSERT_NE(
      audiences_res.status().message().ToBuffer().find("GetJwtAudiences failed: claim not found"),
      std::string::npos)
      << audiences_res.status();
}

TEST(JwtCppUtilTest, GetJwtVerifierSuccess) {
  ASSERT_OK(GetJwtVerifier(testing::PEM_RS256_PUBLIC, "RS256"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_RS384_PUBLIC, "RS384"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_RS512_PUBLIC, "RS512"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_PS256_PUBLIC, "PS256"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_PS384_PUBLIC, "PS384"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_PS512_PUBLIC, "PS512"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_ES256_PUBLIC, "ES256"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_ES384_PUBLIC, "ES384"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_ES512_PUBLIC, "ES512"));
  ASSERT_OK(GetJwtVerifier(testing::PEM_ES256K_PUBLIC, "ES256K"));
}

TEST(JwtCppUtilTest, GetJwtVerifierUnsupported) {
  auto hs256_verifier = GetJwtVerifier("does not matter", "HS256");
  ASSERT_NOK(hs256_verifier);
  ASSERT_NE(
      hs256_verifier.status().message().ToBuffer().find("Unsupported JWT algorithm: HS256"),
      std::string::npos)
      << hs256_verifier.status();
}

TEST(JwtCppUtilTest, GetJwtVerifierInvalidPublicPEM) {
  auto invalid_verifier = GetJwtVerifier("invalidpem", "RS256");
  ASSERT_NOK(invalid_verifier);
  ASSERT_NE(
      invalid_verifier.status().message().ToBuffer().find(
          "Constructing JWT verifier for public key: invalidpem and algo: RS256 failed"),
      std::string::npos)
      << invalid_verifier.status();
}

// 1. Generate a JWT
// 2. Sign it with the given key
// 3. Get the verifier for the key and algorithm
// 4. Assert that verification is successful
#define TEST_VERIFY_JWT_USING_VERIFIER(algorithm, lowercase_algorithm) \
  TEST(JwtCppUtilTest, VerifyJwtUsingVerifierSuccess##algorithm) { \
    auto builder = ASSERT_RESULT(GenerateBuilder(#lowercase_algorithm)); \
    auto token = \
        builder.sign(JWT_ALGORITHM(lowercase_algorithm, testing::PEM_##algorithm##_PRIVATE)); \
    auto verifier = ASSERT_RESULT(GetJwtVerifier(testing::PEM_##algorithm##_PUBLIC, #algorithm)); \
    auto decoded_jwt = ASSERT_RESULT(DecodeJwt(token)); \
    ASSERT_OK(VerifyJwtUsingVerifier(verifier, decoded_jwt)); \
  }

TEST_VERIFY_JWT_USING_VERIFIER(RS256, rs256);
TEST_VERIFY_JWT_USING_VERIFIER(RS384, rs384);
TEST_VERIFY_JWT_USING_VERIFIER(RS512, rs512);
TEST_VERIFY_JWT_USING_VERIFIER(PS256, ps256);
TEST_VERIFY_JWT_USING_VERIFIER(PS384, ps384);
TEST_VERIFY_JWT_USING_VERIFIER(PS512, ps512);
TEST_VERIFY_JWT_USING_VERIFIER(ES256, es256);
TEST_VERIFY_JWT_USING_VERIFIER(ES384, es384);
TEST_VERIFY_JWT_USING_VERIFIER(ES512, es512);
TEST_VERIFY_JWT_USING_VERIFIER(ES256K, es256k);

// TODO: Also add cases for invalid token - expired, issued in future etc. Right now they are
// present in the Java test but can be moved here.

}  // namespace yb::util
