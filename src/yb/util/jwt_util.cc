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

#include "yb/util/jwt_util.h"

#include <jwt-cpp/jwt.h>

#include <string>

#include <glog/logging.h>

#include "yb/gutil/casts.h"
#include "yb/util/jwtcpp_util.h"
#include "yb/util/result.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

using jwt::decoded_jwt;
using jwt::json::type;
using jwt::jwk;
using jwt::jwks;
using jwt::traits::kazuho_picojson;

namespace yb::util {

namespace {

#define OPENSSL_SUCCESS 1

bool DoesValueExist(
    const std::string& value, char* const* values, size_t length, const std::string& field_name) {
  for (size_t idx = 0; idx < length; ++idx) {
    LOG_IF(DFATAL, values[idx] == nullptr)
        << "JWT " << field_name << " unexpectedly NULL for idx " << idx;

    const std::string valueToCompare(values[idx]);
    if (valueToCompare == value) {
      return true;
    }

    VLOG(4) << Format("Mismatch, expected = $0, actual = $1", valueToCompare, value);
  }
  return false;
}

Result<std::string> GetJwkAsPEM(const jwk<kazuho_picojson>& jwk) {
  auto base64urlDecode = [](const std::string& base64url_encoded) {
    return jwt::base::decode<jwt::alphabet::base64url>(
        jwt::base::pad<jwt::alphabet::base64url>(base64url_encoded));
  };

  std::string key_type = VERIFY_RESULT(GetJwkKeyType(jwk));
  if (key_type == "RSA") {
    auto n = VERIFY_RESULT(GetJwkClaimAsString(jwk, "n"));
    auto e = VERIFY_RESULT(GetJwkClaimAsString(jwk, "e"));

    auto modulus = base64urlDecode(n);
    auto exponent = base64urlDecode(e);

    BIGNUM* bnModulus = BN_bin2bn(
        pointer_cast<const unsigned char*>(modulus.data()), narrow_cast<int>(modulus.size()),
        nullptr /* ret */);
    BIGNUM* bnExponent = BN_bin2bn(
        pointer_cast<const unsigned char*>(exponent.data()), narrow_cast<int>(exponent.size()),
        nullptr /* ret */);
    if (bnModulus == nullptr || bnExponent == nullptr) {
      return STATUS(InvalidArgument, "Could not get modulus or exponent of RSA key.");
    }

    RSA* rsa_key = RSA_new();
    if (RSA_set0_key(rsa_key, bnModulus, bnExponent, NULL) != OPENSSL_SUCCESS) {
      return STATUS(InvalidArgument, "Failed to set modulus and exponent to RSA key");
    }

    EVP_PKEY* pkey = EVP_PKEY_new();
    auto res = EVP_PKEY_assign_RSA(pkey, rsa_key);
    if (res != OPENSSL_SUCCESS) {
      return STATUS(InvalidArgument, "Failed to assign private key");
    }

    BIO* pem_bio = BIO_new(BIO_s_mem());
    if (pem_bio == nullptr) {
      return STATUS(InternalError, "Could not create pem_bio");
    }
    if (PEM_write_bio_RSA_PUBKEY(pem_bio, rsa_key) != OPENSSL_SUCCESS) {
      return STATUS(InternalError, "Could not write RSA key into the pem_bio");
    }

    char* pem_data = nullptr;
    size_t pem_size = BIO_get_mem_data(pem_bio, &pem_data);
    std::string pem(pem_data, pem_size);

    BIO_free(pem_bio);
    EVP_PKEY_free(pkey);
    return pem;
  } else if (key_type == "EC") {
    auto x_claim = VERIFY_RESULT(GetJwkClaimAsString(jwk, "x"));
    auto y_claim = VERIFY_RESULT(GetJwkClaimAsString(jwk, "y"));
    auto curve_name = VERIFY_RESULT(GetJwkClaimAsString(jwk, "crv"));

    auto x_coordinate = base64urlDecode(x_claim);
    auto y_coordinate = base64urlDecode(y_claim);

    auto nid = EC_curve_nist2nid(curve_name.c_str());
    if (nid == NID_undef) {
      // P-256K aka secp256k1 is not included in the openssl list of nist2nid lookup table via
      // EC_curve_nist2nid.
      //
      // It is present in the lookup table used in ossl_ec_curve_name2nid function but that is not
      // exposed publicly.
      //
      // So we set a hardcoded value as a hack. This is fine because the NIDs are public values
      // i.e. they should not change between stable releases of openssl.
      if (curve_name == "P-256K" || curve_name == "secp256k1") {
        nid = NID_secp256k1;
      } else {
        return STATUS_FORMAT(
            InvalidArgument, "Could not determine the NID for curve name: $0", curve_name);
      }
    }

    EC_KEY* ec_key = EC_KEY_new_by_curve_name(nid);
    if (ec_key == nullptr) {
      return STATUS_FORMAT(
          InvalidArgument, "Could not create EC_KEY with curve name $0 and nid $1.", curve_name,
          nid);
    }

    BIGNUM* x = BN_bin2bn(
        reinterpret_cast<const unsigned char*>(x_coordinate.data()),
        narrow_cast<int>(x_coordinate.size()), nullptr);
    BIGNUM* y = BN_bin2bn(
        reinterpret_cast<const unsigned char*>(y_coordinate.data()),
        narrow_cast<int>(y_coordinate.size()), nullptr);
    if (x == nullptr || y == nullptr) {
      return STATUS(InvalidArgument, "Could not get x or y coordinates of EC key.");
    }

    if (EC_KEY_set_public_key_affine_coordinates(ec_key, x, y) != OPENSSL_SUCCESS) {
      return STATUS(InvalidArgument, "Could not set public key affine coordinates.");
    }
    EC_KEY_set_asn1_flag(ec_key, OPENSSL_EC_NAMED_CURVE);

    BIO* pem_bio = BIO_new(BIO_s_mem());
    if (pem_bio == nullptr) {
      return STATUS(InternalError, "Could not create pem_bio.");
    }
    if (PEM_write_bio_EC_PUBKEY(pem_bio, ec_key) != OPENSSL_SUCCESS) {
      return STATUS(InternalError, "Could not write EC key into the pem_bio.");
    }

    char* pem_data = nullptr;
    size_t pem_size = BIO_get_mem_data(pem_bio, &pem_data);
    std::string pem(pem_data, pem_size);

    BIO_free(pem_bio);
    EC_KEY_free(ec_key);
    BN_free(x);
    BN_free(y);
    return pem;
  } else {
    return STATUS_FORMAT(NotSupported, "Unsupported key_type: $0", key_type);
  }
}

Status ValidateDecodedJWT(
    const decoded_jwt<kazuho_picojson> decoded_jwt, const jwk<kazuho_picojson> jwk) {
  // To verify the JWT, the library expects the key to be provided in the PEM format (see
  // GetVerifier). Ref: https://github.com/Thalhammer/jwt-cpp/issues/271.
  //
  // The x5c (X.509 Certificate Chain) is an *optional* claim that contains the PEM representation
  // of the X509 certificate. The first certificate of the chain must match the public part of the
  // key used to sign the JWT. Ref: https://datatracker.ietf.org/doc/html/rfc7517#section-4.7
  //
  // Therefor, the easiest way to get the key in PEM format is to extract it out from the x5c
  // claim whenever it is available. So try to get it from the x5c field, else calculate using
  // other JWK fields using openssl.
  std::string key_pem;
  if (GetJwkHasX5c(jwk)) {
    auto x5c = VERIFY_RESULT(GetX5cKeyValueFromJwk(jwk));
    key_pem = VERIFY_RESULT(ConvertX5cDerToPem(x5c));
  } else {
    key_pem = VERIFY_RESULT(GetJwkAsPEM(jwk));
  }
  VLOG(4) << "Serialized pem is:\n" << key_pem << "\n";

  auto algo = VERIFY_RESULT(GetJwtAlgorithm(decoded_jwt));
  auto verifier = VERIFY_RESULT(GetJwtVerifier(key_pem, algo));
  return VerifyJwtUsingVerifier(verifier, decoded_jwt);
}

}  // namespace

Status ValidateJWT(
    const std::string& token, const YBCPgJwtAuthOptions& options,
    std::vector<std::string>* identity_claims) {
  VLOG(4) << Format(
      "Start with token = $0, jwks = $1, matching_claim_key = $2, allowed_issuers = $3, "
      "allowed_audiences = $4",
      token, options.jwks, options.matching_claim_key,
      CStringArrayToString(options.allowed_issuers, options.allowed_issuers_length),
      CStringArrayToString(options.allowed_audiences, options.allowed_audiences_length));

  auto jwks = VERIFY_RESULT(ParseJwks(options.jwks));
  auto decoded_jwt = VERIFY_RESULT(DecodeJwt(token));

  auto key_id = VERIFY_RESULT(GetJwtKeyId(decoded_jwt));
  auto jwk = VERIFY_RESULT(GetJwkFromJwks(jwks, key_id));

  // Validate for signature, expiry and issued_at.
  RETURN_NOT_OK(ValidateDecodedJWT(decoded_jwt, jwk));

  // Validate issuer.
  auto jwt_issuer = VERIFY_RESULT(GetJwtIssuer(decoded_jwt));
  bool valid_issuer = DoesValueExist(
      jwt_issuer, options.allowed_issuers, options.allowed_issuers_length, "issuer");
  if (!valid_issuer) {
    return STATUS_FORMAT(InvalidArgument, "Invalid JWT issuer: $0", jwt_issuer);
  }

  // Validate audiences. A JWT can be issued for more than one audience and is valid as long as one
  // of the audience matches the allowed audiences in the JWT config.
  auto jwt_audiences = VERIFY_RESULT(GetJwtAudiences(decoded_jwt));
  bool valid_audience = false;
  for (const auto& audience : jwt_audiences) {
    valid_audience = DoesValueExist(
        audience, options.allowed_audiences, options.allowed_audiences_length, "audience");
    if (valid_audience) {
      break;
    }
  }
  if (!valid_audience) {
    // We don't add audiences in the error message since there can be many. Also, it is very easy to
    // look up the audiences present in a JWT online.
    return STATUS(InvalidArgument, "Invalid JWT audience(s)");
  }

  // Get the matching claim key and return to the caller.
  auto matching_claim_key = std::string(options.matching_claim_key);
  *identity_claims = VERIFY_RESULT(GetJwtClaimAsStringsList(decoded_jwt, matching_claim_key));

  VLOG(1) << "JWT validation successful";
  return Status::OK();
}

Result<std::string> TEST_GetJwkAsPEM(const jwt::jwk<jwt::traits::kazuho_picojson>& jwk) {
  return GetJwkAsPEM(jwk);
}

}  // namespace yb::util
