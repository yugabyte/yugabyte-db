// Copyright (c) YugaByte, Inc.

#ifndef YB_TABLET_TABLET_RETENTION_POLICY_H
#define YB_TABLET_TABLET_RETENTION_POLICY_H

#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/server/clock.h"

namespace yb {
namespace tablet {

// History retention policy used by a tablet. Currently just keeps history for a fixed amount of
// time. As the next step we need to start tracking pending reads.
class TabletRetentionPolicy : public docdb::HistoryRetentionPolicy {
 public:
  explicit TabletRetentionPolicy(scoped_refptr<yb::server::Clock> clock);
  Timestamp GetHistoryCutoff() override;

 private:
  scoped_refptr<yb::server::Clock> clock_;

  // The delta to be added to the current time to get the history cutoff timestamp. This is always
  // a negative amount.
  MonoDelta retention_delta_;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_TABLET_RETENTION_POLICY_H
