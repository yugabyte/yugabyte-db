// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_retention_policy.h"
#include "yb/gutil/ref_counted.h"
#include "yb/server/hybrid_clock.h"

DEFINE_int32(timestamp_history_retention_interval_sec, 10,
             "The time interval in seconds to retain DocDB history for. This should be "
             "supplemented with read point tracking.");

namespace yb {
namespace tablet {

TabletRetentionPolicy::TabletRetentionPolicy(const Tablet* tablet)
    : tablet_(tablet),
      retention_delta_(MonoDelta::FromSeconds(-FLAGS_timestamp_history_retention_interval_sec)) {}

HybridTime TabletRetentionPolicy::GetHistoryCutoff() {
  return std::min<HybridTime>(
      tablet_->OldestReadPoint(),
      server::HybridClock::AddPhysicalTimeToHybridTime(tablet_->clock()->Now(), retention_delta_));
}

}  // namespace tablet
}  // namespace yb
