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

#pragma once

#include <cstring>

#include "yb/util/monotime.h"
#include "yb/util/string_case.h"

namespace yb {

namespace common {

static const MonoDelta kMaxTtl = MonoDelta::FromNanoseconds(std::numeric_limits<int64_t>::max());

static constexpr int64_t kInvalidTimestamp = std::numeric_limits<int64_t>::min();

// We use an upper bound of int32_t max (in seconds) for Cassandra. Note that this is higher than
// what vanilla Cassandra itself uses, since they store the expiry timestamp in seconds as
// int32_t and have overflow issues. See CASSANDRA-4771.
static const int64_t kCassandraMaxTtlSeconds = std::numeric_limits<int32_t>::max();

static const int64_t kCassandraMinTtlSeconds = 0;

// Verifies whether the TTL provided in milliseconds is valid.
inline static bool IsValidTTLSeconds(int64_t ttl_seconds) {
  return (ttl_seconds >= kCassandraMinTtlSeconds && ttl_seconds <= kCassandraMaxTtlSeconds);
}

static constexpr auto kSpeculativeRetryAlways = "always";
static constexpr auto kSpeculativeRetryMs = "ms";
static constexpr auto kSpeculativeRetryNone = "none";
static constexpr auto kSpeculativeRetryPercentile = "percentile";

static const auto kSpeculativeRetryMsLen = std::strlen(kSpeculativeRetryMs);
static const auto kSpeculativeRetryPercentileLen = std::strlen(kSpeculativeRetryPercentile);

static constexpr auto kCachingKeys = "keys";
static constexpr auto kCachingRowsPerPartition = "rows_per_partition";
static constexpr auto kCachingAll = "ALL";
static constexpr auto kCachingNone = "NONE";

inline static bool IsValidCachingKeysString(const std::string& str) {
  std::string upper_str;
  ToUpperCase(str, &upper_str);
  return (upper_str == kCachingAll || upper_str == kCachingNone);
}

inline static bool IsValidCachingRowsPerPartitionString(const std::string& str) {
  return IsValidCachingKeysString(str);
}

inline static bool IsValidCachingRowsPerPartitionInt(const int64_t val) {
  return val <= std::numeric_limits<int32_t>::max();
}

}  // namespace common
}  // namespace yb
