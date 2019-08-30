// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tserver/backup.pb.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using std::bind;
using std::string;
using consensus::ReplicateMsg;
using consensus::SNAPSHOT_OP;
using consensus::DriverType;
using strings::Substitute;
using yb::tserver::TabletServerErrorPB;
using yb::tserver::TabletSnapshotOpRequestPB;
using yb::tserver::TabletSnapshotOpResponsePB;

// ------------------------------------------------------------------------------------------------
// SnapshotOperationState
// ------------------------------------------------------------------------------------------------

string SnapshotOperationState::ToString() const {
  return Substitute("SnapshotOperationState "
                    "[hybrid_time=$0, request=$1]",
                    hybrid_time().ToString(),
                    request_ == nullptr ? "(none)" : request_->ShortDebugString());
}

void SnapshotOperationState::AcquireSchemaLock(rw_semaphore* l) {
  TRACE("Acquiring schema lock in exclusive mode");
  schema_lock_ = std::unique_lock<rw_semaphore>(*l);
  TRACE("Acquired schema lock");
}

void SnapshotOperationState::ReleaseSchemaLock() {
  CHECK(schema_lock_.owns_lock());
  schema_lock_ = std::unique_lock<rw_semaphore>();
  TRACE("Released schema lock");
}

std::string SnapshotOperationState::GetSnapshotDir(const string& top_snapshots_dir) const {
  if (request_->has_snapshot_dir_override()) {
    return request_->snapshot_dir_override();
  }
  return JoinPathSegments(top_snapshots_dir, request_->snapshot_id());
}

// ------------------------------------------------------------------------------------------------
// SnapshotOperation
// ------------------------------------------------------------------------------------------------

SnapshotOperation::SnapshotOperation(std::unique_ptr<SnapshotOperationState> state)
    : Operation(std::move(state), OperationType::kSnapshot) {}

void SnapshotOperationState::UpdateRequestFromConsensusRound() {
  request_ = consensus_round()->replicate_msg()->mutable_snapshot_request();
}

consensus::ReplicateMsgPtr SnapshotOperation::NewReplicateMsg() {
  auto result = std::make_shared<ReplicateMsg>();
  result->set_op_type(SNAPSHOT_OP);
  result->mutable_snapshot_request()->CopyFrom(*state()->request());
  return result;
}

Status SnapshotOperation::Prepare() {
  TRACE("PREPARE SNAPSHOT: Starting");
  Tablet* tablet = state()->tablet();
  RETURN_NOT_OK(tablet->PrepareForSnapshotOp(state()));

  TRACE("PREPARE SNAPSHOT: finished");
  return Status::OK();
}

void SnapshotOperation::DoStart() {
  state()->TrySetHybridTimeFromClock();

  TRACE("START. HybridTime: $0",
      server::HybridClock::GetPhysicalValueMicros(state()->hybrid_time()));
}

Status SnapshotOperation::DoAborted(const Status& status) {
  TRACE("SnapshotOperation: operation aborted");
  state()->Finish();
  return status;
}

Status SnapshotOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  TRACE("APPLY SNAPSHOT: Starting");
  TabletClass* const tablet = down_cast<TabletClass*>(state()->tablet());
  bool handled = false;

  switch (state()->operation()) {
    case TabletSnapshotOpRequestPB::CREATE: {
      handled = true;
      RETURN_NOT_OK(tablet->CreateSnapshot(state()));
      break;
    }
    case TabletSnapshotOpRequestPB::RESTORE: {
      handled = true;
      RETURN_NOT_OK(tablet->RestoreSnapshot(state()));
      break;
    }
    case TabletSnapshotOpRequestPB::DELETE: {
      handled = true;
      RETURN_NOT_OK(tablet->DeleteSnapshot(state()));
      break;
    }
    case TabletSnapshotOpRequestPB::UNKNOWN: break; // Not handled.
  }

  if (!handled) {
    FATAL_INVALID_ENUM_VALUE(tserver::TabletSnapshotOpRequestPB::Operation, state()->operation());
  }

  // The schema lock was acquired by Tablet::PrepareForCreateSnapshot.
  // Normally, we would release it in tablet.cc after applying the operation,
  // but currently we need to wait until after the COMMIT message is logged
  // to release this lock as a workaround for KUDU-915. See the same TODO in
  // AlterSchemaOperation().
  state()->ReleaseSchemaLock();

  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("SnapshotOperation: making snapshot visible");
  state()->Finish();

  return Status::OK();
}

string SnapshotOperation::ToString() const {
  return Substitute("SnapshotOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
