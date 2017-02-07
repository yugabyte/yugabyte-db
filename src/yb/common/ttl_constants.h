// Copyright (c) YugaByte, Inc.

#ifndef YB_COMMON_TTL_CONSTANTS_H
#define YB_COMMON_TTL_CONSTANTS_H

#include <limits>
#include "yb/util/monotime.h"

namespace yb {
namespace common {

static const MonoDelta kMaxTtl = MonoDelta::FromNanoseconds(
    std::numeric_limits<int64_t>::max());

static const int64_t kMaxTtlMsec = MonoDelta::FromNanoseconds(
    std::numeric_limits<int64_t>::max()).ToMilliseconds();

static const int64_t kMinTtlMsec = 0;

// Verifies whether the TTL provided in milliseconds is valid.
inline static bool isValidTTLMsec(int64_t ttl_msec) {
  return (ttl_msec >= 0 && ttl_msec <= kMaxTtlMsec);
}

}  // namespace common
}  // namespace yb

#endif // YB_COMMON_TTL_CONSTANTS_H
