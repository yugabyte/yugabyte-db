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
#include "yb/dockv/doc_ttl_util.h"

#include "yb/common/schema.h"
#include "yb/dockv/value.h"
#include "yb/util/monotime.h"

using std::string;

namespace yb::dockv {

bool HasExpiredTTL(const HybridTime& key_hybrid_time, const MonoDelta &ttl,
                   const HybridTime& read_hybrid_time) {
  if (ttl.Equals(ValueControlFields::kMaxTtl) || ttl.Equals(ValueControlFields::kResetTtl)) {
    return false;
  }
  return CompareHybridTimesToDelta(key_hybrid_time, read_hybrid_time, ttl) > 0;
}

Result<bool> HasExpiredTTL(const EncodedDocHybridTime& key_hybrid_time, const MonoDelta& ttl,
                           const HybridTime& read_hybrid_time) {
  if (ttl.Equals(ValueControlFields::kMaxTtl) || ttl.Equals(ValueControlFields::kResetTtl)) {
    return false;
  }
  return CompareHybridTimesToDelta(
      VERIFY_RESULT(key_hybrid_time.Decode()).hybrid_time(), read_hybrid_time, ttl) > 0;
}

bool HasExpiredTTL(const HybridTime& expiration_time, const HybridTime& read_hybrid_time) {
  if (expiration_time == kNoExpiration || expiration_time == kUseDefaultTTL) {
    return false;
  }
  return expiration_time < read_hybrid_time;
}

const MonoDelta TableTTL(const Schema& schema) {
  // In this context, a ttl of kMaxTtl indicates that the table has no default TTL.
  MonoDelta ttl = ValueControlFields::kMaxTtl;
  if (schema.table_properties().HasDefaultTimeToLive()) {
    uint64_t default_ttl = schema.table_properties().DefaultTimeToLive();
    return default_ttl == kResetTTL
        ? ValueControlFields::kMaxTtl : MonoDelta::FromMilliseconds(default_ttl);
  }
  return ttl;
}

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const MonoDelta& default_ttl) {
  MonoDelta ttl;
  // When encoded with a value, kMaxTtl indicates that table TTL should be used.
  // If not kMaxTtl, then there is a value-level TTL that we should use instead.
  // A value of kResetTTL (i.e. 0) indicates the value should not expire, in which
  // case kMaxTtl (now representing no expiration) is returned.
  if (!value_ttl.Equals(ValueControlFields::kMaxTtl)) {
    ttl = value_ttl.ToMilliseconds() == kResetTTL ? ValueControlFields::kMaxTtl : value_ttl;
  } else {
    ttl = default_ttl;
  }
  return ttl;
}

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema) {
  return ComputeTTL(value_ttl, TableTTL(schema));
}

const HybridTime FileExpirationFromValueTTL(
    const HybridTime& key_hybrid_time, const MonoDelta& value_ttl) {
  if (value_ttl.Equals(ValueControlFields::kMaxTtl)) {
    return kUseDefaultTTL;
  } else if (value_ttl.Equals(ValueControlFields::kResetTtl)) {
    return kNoExpiration;
  }
  auto exp_time = key_hybrid_time.AddDelta(value_ttl);
  // Sanity check for overflow, return no expiration if detected.
  if (CompareHybridTimesToDelta(key_hybrid_time, exp_time, value_ttl) != 0) {
    return kNoExpiration;
  }
  return exp_time;
}

const HybridTime MaxExpirationFromValueAndTableTTL(const HybridTime& key_hybrid_time,
    const MonoDelta& table_ttl, const HybridTime& value_expiry) {
  // If the max expiration time of values requires the file never expire,
  // then don't expire the file no matter what.
  if (value_expiry == kNoExpiration || key_hybrid_time.is_special()) {
    return kNoExpiration;
  }
  // If the table TTL indicates no expiration, then defer to the value expiration.
  // If the value expiration also indicates to use the table TTL, don't expire the file.
  if (table_ttl.Equals(ValueControlFields::kMaxTtl)) {
    return value_expiry == kUseDefaultTTL ? kNoExpiration : value_expiry;
  }

  // Calculate the expiration time based on table TTL only.
  auto table_expiry = key_hybrid_time.AddDelta(table_ttl);
  // Sanity check for overflow, return no expiration if overflow detected.
  if (CompareHybridTimesToDelta(key_hybrid_time, table_expiry, table_ttl) != 0) {
    return kNoExpiration;
  }
  // Return the greater of the table expiration time and the value expiration time.
  return value_expiry >= table_expiry ? value_expiry : table_expiry;
}

}  // namespace yb::dockv
