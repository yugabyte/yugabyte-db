// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TABLET_TABLET_H
#define ENT_SRC_YB_TABLET_TABLET_H

#include "../../../../src/yb/tablet/tablet.h"

namespace yb {
namespace tablet {

class SnapshotOperationState;

namespace enterprise {

static const char* const kSnapshotsDirName = "snapshots";

class Tablet : public yb::tablet::Tablet {
  typedef yb::tablet::Tablet super;
 public:
  // Create a new tablet.
  Tablet(
      const scoped_refptr<TabletMetadata>& metadata,
      const scoped_refptr<server::Clock>& clock,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      MetricRegistry* metric_registry,
      const scoped_refptr<log::LogAnchorRegistry>& log_anchor_registry,
      const TabletOptions& tablet_options,
      TransactionParticipantContext* transaction_participant_context,
      TransactionCoordinatorContext* transaction_coordinator_context)
      : super(metadata, clock, parent_mem_tracker, metric_registry, log_anchor_registry,
          tablet_options, transaction_participant_context, transaction_coordinator_context) {}

  // Prepares the transaction context for a snapshot operation.
  CHECKED_STATUS PrepareForSnapshotOp(SnapshotOperationState* tx_state);

  // Create snapshot for this tablet.
  // This operation will trigger a flush on the current MemRowSet.
  CHECKED_STATUS CreateSnapshot(SnapshotOperationState* tx_state);

  // Restore snapshot for this tablet.
  CHECKED_STATUS RestoreSnapshot(SnapshotOperationState* tx_state);

  // Restore the RocksDB checkpoint from the provided directory.
  // Only used when table_type_ == YQL_TABLE_TYPE.
  CHECKED_STATUS RestoreCheckpoint(const std::string& dir);

 private:
  DISALLOW_COPY_AND_ASSIGN(Tablet);
};

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb

#endif  // ENT_SRC_YB_TABLET_TABLET_H
