// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet_snapshots.h"
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
  if (!request_->snapshot_dir_override().empty()) {
    return request_->snapshot_dir_override();
  }
  std::string snapshot_id_str;
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(request_->snapshot_id());
  if (!txn_snapshot_id.IsNil()) {
    snapshot_id_str = txn_snapshot_id.ToString();
  } else {
    snapshot_id_str = request_->snapshot_id();
  }

  return JoinPathSegments(top_snapshots_dir, snapshot_id_str);
}

tserver::TabletSnapshotOpRequestPB* SnapshotOperationState::AllocateRequest() {
  request_holder_ = std::make_unique<tserver::TabletSnapshotOpRequestPB>();
  request_ = request_holder_.get();
  return request_holder_.get();
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
  RETURN_NOT_OK(state()->tablet()->snapshots().Prepare(state()));

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
  auto operation = state()->request()->operation();
  switch (operation) {
    case TabletSnapshotOpRequestPB::CREATE_ON_MASTER: {
      auto snapshot_coordinator = state()->tablet()->snapshot_coordinator();
      if (!snapshot_coordinator) {
        return STATUS_FORMAT(IllegalState, "Replicated $0 to tablet without snapshot coordinator",
                             TabletSnapshotOpRequestPB::Operation_Name(operation));
      }
      return snapshot_coordinator->Replicated(leader_term, *state());
    }
    case TabletSnapshotOpRequestPB::CREATE_ON_TABLET: FALLTHROUGH_INTENDED;
    case TabletSnapshotOpRequestPB::RESTORE: FALLTHROUGH_INTENDED;
    case TabletSnapshotOpRequestPB::DELETE:
      return state()->tablet()->snapshots().Replicated(state());
    case google::protobuf::kint32min: FALLTHROUGH_INTENDED;
    case google::protobuf::kint32max: FALLTHROUGH_INTENDED;
    case TabletSnapshotOpRequestPB::UNKNOWN:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(TabletSnapshotOpRequestPB::Operation, operation);
}

string SnapshotOperation::ToString() const {
  return Substitute("SnapshotOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
