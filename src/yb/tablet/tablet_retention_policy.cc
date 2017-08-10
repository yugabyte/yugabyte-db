// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_retention_policy.h"
#include "yb/gutil/ref_counted.h"
#include "yb/common/schema.h"
#include "yb/server/hybrid_clock.h"

DEFINE_int32(timestamp_history_retention_interval_sec, 10,
             "The time interval in seconds to retain DocDB history for. This should be "
             "supplemented with read point tracking.");

namespace yb {
namespace tablet {

using docdb::TableTTL;

TabletRetentionPolicy::TabletRetentionPolicy(const Tablet* tablet)
    : tablet_(tablet),
      retention_delta_(MonoDelta::FromSeconds(-FLAGS_timestamp_history_retention_interval_sec)) {}

HybridTime TabletRetentionPolicy::GetHistoryCutoff() {
  return std::min<HybridTime>(
      tablet_->OldestReadPoint(),
      server::HybridClock::AddPhysicalTimeToHybridTime(tablet_->clock()->Now(), retention_delta_));
}

ColumnIdsPtr TabletRetentionPolicy::GetDeletedColumns() {
  HybridTime history_cutoff = GetHistoryCutoff();
  // We're getting history cutoff and deleted columns separately, so they could be inconsistent.
  // This is not a problem because history cutoff only monotonically increases, so an inconsistency
  // means that we might not compact all the columns we should be able to at the present time, but
  // those will be processed in the next compaction.
  std::shared_ptr<ColumnIds> deleted_before_history_cutoff = std::make_shared<ColumnIds>();
  for (auto deleted_col : tablet_->metadata()->GetDeletedColumns()) {
    if (deleted_col.ht < history_cutoff) {
      deleted_before_history_cutoff->insert(deleted_col.id);
    }
  }
  return deleted_before_history_cutoff;
}

MonoDelta TabletRetentionPolicy::GetTableTTL() {
  return TableTTL(tablet_->metadata()->schema());
}

}  // namespace tablet
}  // namespace yb
