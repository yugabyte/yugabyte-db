// Copyright (c) YugaByte, Inc.

#pragma once

#include "yb/util/flags.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/physical_time.h"

DECLARE_int32(leader_lease_duration_ms);
DECLARE_int32(ht_lease_duration_ms);

namespace yb {
namespace consensus {

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
struct LeaseData {
  typedef LeaseTraits<Time> Traits;

  static Time NoneValue() {
    return Traits::NoneValue();
  }

  LeaseData() : expiration(NoneValue()) {}

  LeaseData(std::string holder_uuid_, const Time& expiration_)
      : holder_uuid(std::move(holder_uuid_)), expiration(expiration_) {}

  // UUID of node that holds leader lease.
  std::string holder_uuid;

  Time expiration;

  void Reset() {
    expiration = NoneValue();
    holder_uuid.clear();
  }

  void TryUpdate(const LeaseData& rhs) {
    if (rhs.expiration > expiration) {
      expiration = rhs.expiration;
      if (rhs.holder_uuid != holder_uuid) {
        holder_uuid = rhs.holder_uuid;
      }
    }
  }

  explicit operator bool() const {
    return expiration != NoneValue();
  }
};

typedef LeaseData<CoarseTimePoint> CoarseTimeLease;

typedef LeaseData<MicrosTime> PhysicalComponentLease;

} // namespace consensus
} // namespace yb
