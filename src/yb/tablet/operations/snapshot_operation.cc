// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tserver/backup.pb.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/flag_tags.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, modify_flushed_frontier_snapshot_op, true,
                 "Whether to modify flushed frontier after "
                 "a create snapshot operation.");

namespace yb {
namespace tablet {

using std::bind;
using std::string;
using consensus::ReplicateMsg;
using consensus::SNAPSHOT_OP;
using consensus::DriverType;
using strings::Substitute;
using yb::tserver::TabletServerError;
using yb::tserver::TabletServerErrorPB;
using yb::tserver::TabletSnapshotOpRequestPB;

// ------------------------------------------------------------------------------------------------
// SnapshotOperationState
// ------------------------------------------------------------------------------------------------

string SnapshotOperationState::ToString() const {
  return Format("SnapshotOperationState { hybrid_time: $0 request: $1 }",
                hybrid_time(), request());
}

Result<std::string> SnapshotOperationState::GetSnapshotDir() const {
  auto& request = *this->request();
  if (!request.snapshot_dir_override().empty()) {
    return request.snapshot_dir_override();
  }
  if (request.snapshot_id().empty()) {
    return std::string();
  }
  std::string snapshot_id_str;
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(request.snapshot_id());
  if (txn_snapshot_id) {
    snapshot_id_str = txn_snapshot_id.ToString();
  } else {
    snapshot_id_str = request.snapshot_id();
  }

  return JoinPathSegments(VERIFY_RESULT(tablet()->metadata()->TopSnapshotsDir()), snapshot_id_str);
}

Status SnapshotOperationState::DoCheckOperationRequirements() {
  if (operation() != TabletSnapshotOpRequestPB::RESTORE_ON_TABLET) {
    return Status::OK();
  }

  const string snapshot_dir = VERIFY_RESULT(GetSnapshotDir());
  if (snapshot_dir.empty()) {
    return Status::OK();
  }
  Status s = tablet()->rocksdb_env().FileExists(snapshot_dir);

  if (!s.ok()) {
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::INVALID_SNAPSHOT)).
             CloneAndPrepend(Format("Snapshot dir: $0", snapshot_dir));
  }

  return Status::OK();
}

bool SnapshotOperationState::CheckOperationRequirements() {
  auto status = DoCheckOperationRequirements();
  if (status.ok()) {
    return true;
  }

  // LogPrefix() calls ToString() which needs correct hybrid_time.
  TrySetHybridTimeFromClock();
  LOG_WITH_PREFIX(WARNING) << status;
  TRACE("Requirements was not satisfied for snapshot operation: $0", operation());
  // Run the callback, finish RPC and return the error to the sender.
  CompleteWithStatus(status);
  Finish();
  return false;
}

Result<SnapshotCoordinator&> GetSnapshotCoordinator(SnapshotOperationState* state) {
  auto snapshot_coordinator = state->tablet()->snapshot_coordinator();
  if (!snapshot_coordinator) {
    return STATUS_FORMAT(IllegalState, "Replicated $0 to tablet without snapshot coordinator",
                         TabletSnapshotOpRequestPB::Operation_Name(state->request()->operation()));
  }
  return *snapshot_coordinator;
}

Status SnapshotOperationState::Apply(int64_t leader_term) {
  TRACE("APPLY SNAPSHOT: Starting");
  auto operation = request()->operation();
  switch (operation) {
    case TabletSnapshotOpRequestPB::CREATE_ON_MASTER:
      return VERIFY_RESULT(GetSnapshotCoordinator(this)).get().CreateReplicated(leader_term, *this);
    case TabletSnapshotOpRequestPB::DELETE_ON_MASTER:
      return VERIFY_RESULT(GetSnapshotCoordinator(this)).get().DeleteReplicated(leader_term, *this);
    case TabletSnapshotOpRequestPB::RESTORE_SYS_CATALOG:
      return VERIFY_RESULT(GetSnapshotCoordinator(this)).get().RestoreSysCatalogReplicated(
          leader_term, *this);
    case TabletSnapshotOpRequestPB::CREATE_ON_TABLET:
      return tablet()->snapshots().Create(this);
    case TabletSnapshotOpRequestPB::RESTORE_ON_TABLET:
      return tablet()->snapshots().Restore(this);
    case TabletSnapshotOpRequestPB::DELETE_ON_TABLET:
      return tablet()->snapshots().Delete(*this);
    case google::protobuf::kint32min: FALLTHROUGH_INTENDED;
    case google::protobuf::kint32max: FALLTHROUGH_INTENDED;
    case TabletSnapshotOpRequestPB::UNKNOWN:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(TabletSnapshotOpRequestPB::Operation, operation);
}

Status SnapshotOperationState::ApplyAndFlushFrontier(int64_t leader_term) {
  RETURN_NOT_OK(Apply(leader_term));
  // Record the fact that we've executed the "create snapshot" Raft operation. We are not forcing
  // the flushed frontier to have this exact value, although in practice it will, since this is the
  // latest operation we've ever executed in this Raft group. This way we keep the current value
  // of history cutoff.
  if (FLAGS_TEST_modify_flushed_frontier_snapshot_op) {
    docdb::ConsensusFrontier frontier;
    frontier.set_op_id(op_id());
    frontier.set_hybrid_time(hybrid_time());
    LOG(INFO) << "Forcing modify flushed frontier to " << frontier.op_id();
    return tablet()->ModifyFlushedFrontier(
        frontier, rocksdb::FrontierModificationMode::kUpdate);
  } else {
    return Status::OK();
  }
}

// ------------------------------------------------------------------------------------------------
// SnapshotOperation
// ------------------------------------------------------------------------------------------------

SnapshotOperation::SnapshotOperation(std::unique_ptr<SnapshotOperationState> state)
    : Operation(std::move(state), OperationType::kSnapshot) {}

void SnapshotOperationState::UpdateRequestFromConsensusRound() {
  UseRequest(consensus_round()->replicate_msg()->mutable_snapshot_request());
}

consensus::ReplicateMsgPtr SnapshotOperation::NewReplicateMsg() {
  auto result = std::make_shared<ReplicateMsg>();
  result->set_op_type(SNAPSHOT_OP);
  auto request = state()->ReleaseRequest();
  if (request) {
    result->set_allocated_snapshot_request(request);
  } else {
    *result->mutable_snapshot_request() = *state()->request();
  }
  return result;
}

Status SnapshotOperation::Prepare() {
  TRACE("PREPARE SNAPSHOT: Starting");
  RETURN_NOT_OK(state()->tablet()->snapshots().Prepare(this));

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
  auto status = state()->ApplyAndFlushFrontier(leader_term);
  state()->Finish();
  RETURN_NOT_OK(status);
  return Status::OK();
}

string SnapshotOperation::ToString() const {
  return Substitute("SnapshotOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
