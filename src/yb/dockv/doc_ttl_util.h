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

// TTL helper methods are used in the DocDB code.

#pragma once

#include <string>

#include "yb/common/common_fwd.h"
#include "yb/common/hybrid_time.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"

namespace yb::dockv {

// Determines whether or not the TTL for a key has expired, given the ttl for the key, its hybrid
// time and the hybrid_time we're reading at.
bool HasExpiredTTL(const HybridTime& key_hybrid_time, const MonoDelta& ttl,
                   const HybridTime& read_hybrid_time);

Result<bool> HasExpiredTTL(const EncodedDocHybridTime& key_hybrid_time, const MonoDelta& ttl,
                           const HybridTime& read_hybrid_time);

// Determines whether an expiration time has already passed (with special cases).
bool HasExpiredTTL(const HybridTime& expiration_time, const HybridTime& read_hybrid_time);

// Computes the table level TTL, given a schema.
const MonoDelta TableTTL(const Schema& schema);

// Computes the effective TTL by combining the column level TTL with the default table level TTL.
const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const MonoDelta& default_ttl);

// Utility function that computes the effective TTL directly given a schema
const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema);

// Utility function that computes an expiration time based on a key's hybrid_time and TTL.
const HybridTime FileExpirationFromValueTTL(
    const HybridTime& key_hybrid_time, const MonoDelta& value_ttl);

// Utility function to calculate the maximum expiration time of a file based on its maximum value
// expiration time and its table TTL.
const HybridTime MaxExpirationFromValueAndTableTTL(const HybridTime& key_hybrid_time,
    const MonoDelta& table_ttl, const HybridTime& value_expiry);

// Cassandra considers a TTL of zero as resetting the TTL.
static const uint64_t kResetTTL = 0;

// kUseDefaultTTL indicates that column TTL expiration should defer to table TTL expiration.
// It should always be overwritten by any explicit value (set to kInitial).
static const HybridTime kUseDefaultTTL(HybridTime::kInitial);

// kNoExpiration indicates that a file should not be expired.
// It should always dominate any explicit expiration time (set to kMax).
static const HybridTime kNoExpiration(HybridTime::kMax);

}  // namespace yb::dockv
