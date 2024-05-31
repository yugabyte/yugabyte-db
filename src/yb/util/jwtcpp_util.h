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

#pragma once

#include <jwt-cpp/jwt.h>

#include <set>
#include <string>

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

// This file contains utility wrappers & extensions functions over the JWT-CPP library.
//
// The library throws exceptions, hence each function that we need in our codebase has a wrapper
// which catches the exception and return a Result.
//
// Additionally, we also have functions which combine a few related functionalities into one for
// ease of use of the callers. For e.g. GetClaimAsStringsArray, GetVerifier etc.

// TODO:
// 1. Add wrapper types over all the JWT-CPP types we use so that we never have to include the
//    JWT-CPP header outside of this wrapper. This will help avoid cases where we accidently call a
//    function on the type which throws exception.
// 2. Add a lint rule validating that we don't use JWT-CPP header outside of this wrapper.

namespace yb::util {

//--------------------------------------------------------------------------------------------------
// JWK and JWKS.

Result<jwt::jwks<jwt::traits::kazuho_picojson>> ParseJwks(const std::string& key_set) noexcept;

Result<jwt::jwk<jwt::traits::kazuho_picojson>> GetJwkFromJwks(
    const jwt::jwks<jwt::traits::kazuho_picojson>& jwks, const std::string& key_id) noexcept;

Result<std::string> GetX5cKeyValueFromJwk(
    const jwt::jwk<jwt::traits::kazuho_picojson>& jwk) noexcept;

Result<std::string> GetJwkKeyType(const jwt::jwk<jwt::traits::kazuho_picojson>& jwk) noexcept;

Result<std::string> GetJwkKeyId(const jwt::jwk<jwt::traits::kazuho_picojson>& jwk) noexcept;

bool GetJwkHasX5c(const jwt::jwk<jwt::traits::kazuho_picojson>& jwk) noexcept;

Result<std::string> GetJwkClaimAsString(
    const jwt::jwk<jwt::traits::kazuho_picojson>& jwk, const std::string& key) noexcept;

Result<std::string> ConvertX5cDerToPem(const std::string& x5c) noexcept;

//--------------------------------------------------------------------------------------------------
// JWT.

Result<jwt::decoded_jwt<jwt::traits::kazuho_picojson>> DecodeJwt(const std::string& token) noexcept;

Result<std::string> GetJwtKeyId(
    const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded_jwt) noexcept;

Result<std::string> GetJwtIssuer(
    const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded_jwt) noexcept;

Result<std::set<std::string>> GetJwtAudiences(
    const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded_jwt) noexcept;

Result<std::vector<std::string>> GetJwtClaimAsStringsList(
    const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded_jwt,
    const std::string& name) noexcept;

Result<std::string> GetJwtAlgorithm(
    const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded_jwt) noexcept;

Result<jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson>> GetJwtVerifier(
    const std::string& key_pem, const std::string& algo) noexcept;

Status VerifyJwtUsingVerifier(
    const jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson>& verifier,
    const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoded_jwt) noexcept;

}  // namespace yb::util
