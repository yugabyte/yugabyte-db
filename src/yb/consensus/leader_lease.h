// Copyright (c) YugaByte, Inc.

#pragma once

#include "yb/util/flags.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/physical_time.h"

DECLARE_int32(leader_lease_duration_ms);
DECLARE_int32(ht_lease_duration_ms);

namespace yb::consensus {

YB_DEFINE_ENUM(LeaderLeaseCheckMode, (NEED_LEASE)(DONT_NEED_LEASE))

template <class Time>
struct LeaseTraits;

template <>
struct LeaseTraits<CoarseTimePoint> {
  static CoarseTimePoint NoneValue() {
    return CoarseTimePoint::min();
  }
};

template <>
struct LeaseTraits<MicrosTime> {
  static MicrosTime NoneValue() {
    return 0;
  }
};

template <class Time>
struct LeaseUpdate {
  std::string holder_uuid;
  Time expiration;

  explicit operator bool() const {
    return !holder_uuid.empty();
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(holder_uuid, expiration);
  }
};

template <class Time>
bool operator<(const LeaseUpdate<Time>& lhs, const LeaseUpdate<Time>& rhs) {
  return lhs.expiration < rhs.expiration ||
         (lhs.expiration == rhs.expiration && lhs.holder_uuid < rhs.holder_uuid);
}

template <class Time>
struct LeaseData {
  using Traits = LeaseTraits<Time>;

  static Time NoneValue() {
    return Traits::NoneValue();
  }

  LeaseData() {}

  using Leases = std::vector<LeaseUpdate<Time>>;
  Leases leases;

  void Reset(const std::string& holder_uuid) {
    auto it = Find(holder_uuid);
    if (it != leases.end()) {
      leases.erase(it);
    }
  }

  void Update(const LeaseUpdate<Time>& rhs) {
    DCHECK(rhs);
    auto it = Find(rhs.holder_uuid);
    if (it == leases.end()) {
      leases.push_back(rhs);
    } else {
      it->expiration = std::max(rhs.expiration, it->expiration);
    }
  }

  // Remove leases that have expired before now, and return the max remaining lease time.
  LeaseUpdate<Time> Cleanup(Time now) {
    LeaseUpdate<Time> max_remaining;
    if (leases.empty()) {
      return max_remaining;
    }
    std::erase_if(leases, [&max_remaining, now](const auto& lease) {
      if (lease.expiration < now) {
        return true;
      }
      if (!max_remaining || lease.expiration > max_remaining.expiration) {
        max_remaining = lease;
      }
      return false;
    });
    return max_remaining;
  }

  // Remove leases based on the filter and return the list of removed leases.
  template <class Filter>
  Leases CleanupAccepted(const Filter& filter) {
    Leases result;
    std::erase_if(leases, [&filter, &result](const auto& lease) {
      if (!filter(lease.holder_uuid)) {
        return false;
      }
      result.push_back(lease);
      return true;
    });
    return result;
  }

  explicit operator bool() const {
    return !leases.empty();
  }

  bool ExpiredAt(Time time) const {
    for (const auto& lease : leases) {
      if (lease.expiration >= time) {
        return false;
      }
    }
    return true;
  }

 private:
  Leases::iterator Find(const std::string& holder_uuid) {
    return std::find_if(leases.begin(), leases.end(), [&holder_uuid](const auto& entry) {
      return entry.holder_uuid == holder_uuid;
    });
  }
};

using CoarseTimeLeaseUpdate = LeaseUpdate<CoarseTimePoint>;
using CoarseTimeLease = LeaseData<CoarseTimePoint>;
using PhysicalComponentLeaseUpdate = LeaseUpdate<MicrosTime>;
using PhysicalComponentLease = LeaseData<MicrosTime>;

} // namespace yb::consensus
