// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_retention_policy.h"
#include "yb/gutil/ref_counted.h"
#include "yb/server/hybrid_clock.h"

DEFINE_int32(timestamp_history_retention_interval_sec, 10,
             "The time interval in seconds to retain DocDB history for. This should be "
             "supplemented with read point tracking.");

namespace yb {
namespace tablet {

TabletRetentionPolicy::TabletRetentionPolicy(scoped_refptr<yb::server::Clock> clock)
    : clock_(clock),
      retention_delta_(MonoDelta::FromSeconds(-FLAGS_timestamp_history_retention_interval_sec)) {
}

Timestamp TabletRetentionPolicy::GetHistoryCutoff() {
  return server::HybridClock::AddPhysicalTimeToTimestamp(clock_->Now(), retention_delta_);
}

}  // namespace tablet
}  // namespace yb
