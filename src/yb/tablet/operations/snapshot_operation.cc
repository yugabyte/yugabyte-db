// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus_round.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tserver/backup.pb.h"
#include "yb/tserver/tserver_error.h"
#include "yb/util/trace.h"

DEFINE_bool(consistent_restore, false, "Whether to enable consistent restoration of snapshots");

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
                hybrid_time_even_if_unset(), request());
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
  LOG_WITH_PREFIX(WARNING) << status;
  TRACE("Requirements was not satisfied for snapshot operation: $0", operation());
  // Run the callback, finish RPC and return the error to the sender.
  CompleteWithStatus(status);
  Release();
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
      return tablet()->snapshots().Delete(this);
    case TabletSnapshotOpRequestPB::RESTORE_FINISHED:
      return tablet()->snapshots().RestoreFinished(this);
    case google::protobuf::kint32min: FALLTHROUGH_INTENDED;
    case google::protobuf::kint32max: FALLTHROUGH_INTENDED;
    case TabletSnapshotOpRequestPB::UNKNOWN:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(TabletSnapshotOpRequestPB::Operation, operation);
}

void SnapshotOperationState::AddedAsPending() {
  if (request()->operation() == TabletSnapshotOpRequestPB::RESTORE_ON_TABLET) {
    tablet()->RegisterOperationFilter(this);
  }
}

void SnapshotOperationState::RemovedFromPending() {
  if (request()->operation() == TabletSnapshotOpRequestPB::RESTORE_ON_TABLET) {
    tablet()->UnregisterOperationFilter(this);
  }
}

Status SnapshotOperationState::RejectionStatus(
    OpId rejected_op_id, consensus::OperationType op_type) {
  return STATUS_FORMAT(
      IllegalState, "Operation $0 ($1) is not allowed during restore",
      OperationType_Name(op_type), rejected_op_id);
}

bool SnapshotOperationState::ShouldAllowOpDuringRestore(consensus::OperationType op_type) {
  switch (op_type) {
    case consensus::NO_OP: FALLTHROUGH_INTENDED;
    case consensus::UNKNOWN_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_METADATA_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_CONFIG_OP: FALLTHROUGH_INTENDED;
    case consensus::HISTORY_CUTOFF_OP: FALLTHROUGH_INTENDED;
    case consensus::SNAPSHOT_OP: FALLTHROUGH_INTENDED;
    case consensus::TRUNCATE_OP: FALLTHROUGH_INTENDED;
    case consensus::SPLIT_OP:
      return true;
    case consensus::UPDATE_TRANSACTION_OP: FALLTHROUGH_INTENDED;
    case consensus::WRITE_OP:
      return !FLAGS_consistent_restore;
  }
  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, op_type);
}

Status SnapshotOperationState::CheckOperationAllowed(
    const OpId& id, consensus::OperationType op_type) const {
  if (id == op_id() || ShouldAllowOpDuringRestore(op_type)) {
    return Status::OK();
  }

  return RejectionStatus(id, op_type);
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

Status SnapshotOperation::DoAborted(const Status& status) {
  TRACE("SnapshotOperation: operation aborted");
  return status;
}

Status SnapshotOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  return state()->Apply(leader_term);
}

string SnapshotOperation::ToString() const {
  return Substitute("SnapshotOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
