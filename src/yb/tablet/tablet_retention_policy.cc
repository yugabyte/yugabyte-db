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

#include "yb/gutil/ref_counted.h"
#include "yb/common/schema.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_retention_policy.h"

DEFINE_int32(timestamp_history_retention_interval_sec, 10,
             "The time interval in seconds to retain DocDB history for. This should be "
             "supplemented with read point tracking.");

namespace yb {
namespace tablet {

using docdb::TableTTL;
using docdb::HistoryRetentionDirective;

TabletRetentionPolicy::TabletRetentionPolicy(Tablet* tablet)
    : tablet_(tablet),
      retention_delta_(MonoDelta::FromSeconds(-FLAGS_timestamp_history_retention_interval_sec)) {
}

HistoryRetentionDirective TabletRetentionPolicy::GetRetentionDirective() {
  // We try to garbage-collect history older than current time minus the configured retention
  // interval, but we might not be able to do so if there are still read operations reading at an
  // older snapshot.
  const HybridTime proposed_cutoff =
      server::HybridClock::AddPhysicalTimeToHybridTime(tablet_->clock()->Now(), retention_delta_);
  const HybridTime history_cutoff = tablet_->UpdateHistoryCutoff(proposed_cutoff);

  std::shared_ptr<ColumnIds> deleted_before_history_cutoff = std::make_shared<ColumnIds>();
  for (auto deleted_col : tablet_->metadata()->deleted_cols()) {
    if (deleted_col.ht < history_cutoff) {
      deleted_before_history_cutoff->insert(deleted_col.id);
    }
  }

  return {
    history_cutoff,
    std::move(deleted_before_history_cutoff),
    TableTTL(tablet_->metadata()->schema())
  };
}

}  // namespace tablet
}  // namespace yb
