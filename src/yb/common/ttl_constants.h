// Copyright (c) YugaByte, Inc.

#ifndef YB_COMMON_TTL_CONSTANTS_H
#define YB_COMMON_TTL_CONSTANTS_H

#include <limits>
#include "yb/util/monotime.h"

namespace yb {
namespace common {

static const MonoDelta kMaxTtl = MonoDelta::FromNanoseconds(
    std::numeric_limits<int64_t>::max());

static const int64_t kMaxTtlSeconds = kMaxTtl.ToSeconds();

static const int64_t kMinTtlSeconds = 0;

// Verifies whether the TTL provided in milliseconds is valid.
inline static bool isValidTTLSeconds(int64_t ttl_seconds) {
  return (ttl_seconds >= 0 && ttl_seconds <= kMaxTtlSeconds);
}

}  // namespace common
}  // namespace yb

#endif // YB_COMMON_TTL_CONSTANTS_H
