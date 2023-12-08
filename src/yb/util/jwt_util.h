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

#include "yb/util/status.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::util {

// Validate the provided JWT based on the authentication options.
// Populates the identity claims from the JWT into identity_claims.
Status ValidateJWT(
    const std::string& token, const YBCPgJwtAuthOptions& options,
    std::vector<std::string>* identity_claims);

Result<std::string> TEST_GetJwkAsPEM(const jwt::jwk<jwt::traits::kazuho_picojson>& jwk);

}  // namespace yb::util
