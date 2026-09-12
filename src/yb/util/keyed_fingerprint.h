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

#pragma once

#include <string>

#include "yb/util/slice.h"

namespace yb {

// Keyed diagnostic fingerprints: non-reversible, scope-limited correlation tokens for values
// that must never appear in logs, errors, or support bundles. Safe for low-cardinality values
// because confirming a candidate requires the key; limiting the key's lifetime to one scope
// (e.g. one job) limits how long that confirmation is possible at all.

// 32 crypto-random bytes (OpenSSL RAND_bytes).
std::string GenerateFingerprintKey();

// Renders "<tag8hex>-<digest16hex>". The tag is derived from the key alone, so every token
// under one key shares it: two tokens with different tags are visibly incomparable. The
// digest is HMAC-SHA256(key, data), truncated -- the required property is unconfirmability
// without the key, not collision resistance against a key holder. The 4-byte tag is a
// visual aid, not a boundary: distinct keys collide on it around 2^16 concurrent scopes
// (birthday), which can only make two incomparable tokens look comparable.
std::string KeyedFingerprintToken(Slice key, Slice data);

}  // namespace yb
