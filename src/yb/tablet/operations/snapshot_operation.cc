// Copyright (c) YugaByte, Inc.

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/common/snapshot.h"

#include "yb/consensus/consensus_round.h"
#include "yb/consensus/consensus.messages.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.pb.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

using std::string;

DEFINE_UNKNOWN_bool(
    consistent_restore, true, "Whether to enable consistent restoration of snapshots");

DEFINE_test_flag(bool, modify_flushed_frontier_snapshot_op, true,
                 "Whether to modify flushed frontier after "
                 "a create snapshot operation.");

namespace yb {
namespace tablet {

using tserver::LWTabletSnapshotOpRequestPB;
using tserver::TabletServerError;
using tserver::TabletServerErrorPB;
using tserver::TabletSnapshotOpRequestPB;

template <>
void RequestTraits<LWTabletSnapshotOpRequestPB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWTabletSnapshotOpRequestPB* request) {
  replicate->ref_snapshot_request(request);
}

template <>
LWTabletSnapshotOpRequestPB* RequestTraits<LWTabletSnapshotOpRequestPB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_snapshot_request();
}

Result<std::string> SnapshotOperation::GetSnapshotDir() const {
  auto& request = *this->request();
  if (!request.snapshot_dir_override().empty()) {
    return request.snapshot_dir_override().ToBuffer();
  }
  if (request.snapshot_id().empty()) {
    return std::string();
  }
  std::string snapshot_id_str;
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(request.snapshot_id());
  if (txn_snapshot_id) {
    snapshot_id_str = txn_snapshot_id.ToString();
  } else {
    snapshot_id_str = request.snapshot_id().ToBuffer();
  }

  auto tablet = VERIFY_RESULT(tablet_safe());
  return JoinPathSegments(VERIFY_RESULT(tablet->metadata()->TopSnapshotsDir()), snapshot_id_str);
}

Status SnapshotOperation::DoCheckOperationRequirements() {
  if (operation() != TabletSnapshotOpRequestPB::RESTORE_ON_TABLET) {
    return Status::OK();
  }

  const string snapshot_dir = VERIFY_RESULT(GetSnapshotDir());
  if (snapshot_dir.empty()) {
    return Status::OK();
  }
  Status s = VERIFY_RESULT(tablet_safe())->rocksdb_env().FileExists(snapshot_dir);

  if (!s.ok()) {
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::INVALID_SNAPSHOT)).
             CloneAndPrepend(Format("Snapshot dir: $0", snapshot_dir));
  }

  return Status::OK();
}

bool SnapshotOperation::CheckOperationRequirements() {
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

Result<SnapshotCoordinator&> GetSnapshotCoordinator(SnapshotOperation* operation) {
  auto snapshot_coordinator = VERIFY_RESULT(operation->tablet_safe())->snapshot_coordinator();
  if (!snapshot_coordinator) {
    return STATUS_FORMAT(IllegalState, "Replicated $0 to tablet without snapshot coordinator",
                         TabletSnapshotOpRequestPB::Operation_Name(
                             operation->request()->operation()));
  }
  return *snapshot_coordinator;
}

Status SnapshotOperation::Apply(int64_t leader_term, Status* complete_status) {
  TRACE("APPLY SNAPSHOT: Starting");
  auto operation = request()->operation();
  switch (operation) {
    case TabletSnapshotOpRequestPB::CREATE_ON_MASTER:
      return VERIFY_RESULT(GetSnapshotCoordinator(this)).get().CreateReplicated(leader_term, *this);
    case TabletSnapshotOpRequestPB::DELETE_ON_MASTER:
      return VERIFY_RESULT(GetSnapshotCoordinator(this)).get().DeleteReplicated(leader_term, *this);
    case TabletSnapshotOpRequestPB::RESTORE_SYS_CATALOG:
      return VERIFY_RESULT(GetSnapshotCoordinator(this)).get().RestoreSysCatalogReplicated(
          leader_term, *this, complete_status);
    case TabletSnapshotOpRequestPB::CREATE_ON_TABLET:
      return VERIFY_RESULT(tablet_safe())->snapshots().Create(this);
    case TabletSnapshotOpRequestPB::RESTORE_ON_TABLET:
      return VERIFY_RESULT(tablet_safe())->snapshots().Restore(this);
    case TabletSnapshotOpRequestPB::DELETE_ON_TABLET:
      return VERIFY_RESULT(tablet_safe())->snapshots().Delete(*this);
    case TabletSnapshotOpRequestPB::RESTORE_FINISHED:
      return VERIFY_RESULT(tablet_safe())->snapshots().RestoreFinished(this);
    case google::protobuf::kint32min: FALLTHROUGH_INTENDED;
    case google::protobuf::kint32max: FALLTHROUGH_INTENDED;
    case TabletSnapshotOpRequestPB::UNKNOWN:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(TabletSnapshotOpRequestPB::Operation, operation);
}

bool SnapshotOperation::NeedOperationFilter() const {
  return request()->operation() == TabletSnapshotOpRequestPB::RESTORE_ON_TABLET ||
         request()->operation() == TabletSnapshotOpRequestPB::RESTORE_SYS_CATALOG;
}

void SnapshotOperation::AddedAsPending(const TabletPtr& tablet) {
  if (NeedOperationFilter()) {
    tablet->RegisterOperationFilter(this);
  }
}

void SnapshotOperation::RemovedFromPending(const TabletPtr& tablet) {
  if (NeedOperationFilter()) {
    tablet->UnregisterOperationFilter(this);
  }
}

Status SnapshotOperation::RejectionStatus(
    OpId rejected_op_id, consensus::OperationType op_type) {
  return STATUS_FORMAT(
      IllegalState, "Operation $0 ($1) is not allowed during restore",
      OperationType_Name(op_type), rejected_op_id);
}

bool SnapshotOperation::ShouldAllowOpDuringRestore(consensus::OperationType op_type) {
  switch (op_type) {
    case consensus::NO_OP: FALLTHROUGH_INTENDED;
    case consensus::UNKNOWN_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_METADATA_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_CONFIG_OP: FALLTHROUGH_INTENDED;
    case consensus::HISTORY_CUTOFF_OP: FALLTHROUGH_INTENDED;
    case consensus::SNAPSHOT_OP: FALLTHROUGH_INTENDED;
    case consensus::TRUNCATE_OP: FALLTHROUGH_INTENDED;
    case consensus::SPLIT_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_AUTO_FLAGS_CONFIG_OP:
    case consensus::CLONE_OP:
      return true;
    case consensus::UPDATE_TRANSACTION_OP: FALLTHROUGH_INTENDED;
    case consensus::WRITE_OP:
      return !FLAGS_consistent_restore;
  }
  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, op_type);
}

Status SnapshotOperation::CheckOperationAllowed(
    const OpId& id, consensus::OperationType op_type) const {
  if (id == op_id() || ShouldAllowOpDuringRestore(op_type)) {
    return Status::OK();
  }

  return RejectionStatus(id, op_type);
}

// ------------------------------------------------------------------------------------------------
// SnapshotOperation
// ------------------------------------------------------------------------------------------------

Status SnapshotOperation::Prepare(IsLeaderSide is_leader_side) {
  TRACE("PREPARE SNAPSHOT: Starting");
  RETURN_NOT_OK(VERIFY_RESULT(tablet_safe())->snapshots().Prepare(this));

  TRACE("PREPARE SNAPSHOT: finished");
  return Status::OK();
}

Status SnapshotOperation::DoAborted(const Status& status) {
  TRACE("SnapshotOperation: operation aborted");
  return status;
}

Status SnapshotOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  RETURN_NOT_OK(Apply(leader_term, complete_status));
  // Record the fact that we've executed the "create snapshot" Raft operation. We are not forcing
  // the flushed frontier to have this exact value, although in practice it will, since this is the
  // latest operation we've ever executed in this Raft group. This way we keep the current value
  // of history cutoff.
  if (FLAGS_TEST_modify_flushed_frontier_snapshot_op) {
    docdb::ConsensusFrontier frontier;
    frontier.set_op_id(op_id());
    frontier.set_hybrid_time(hybrid_time());
    LOG(INFO) << "Forcing modify flushed frontier to " << frontier.op_id();
    return VERIFY_RESULT(tablet_safe())->ModifyFlushedFrontier(
        frontier, rocksdb::FrontierModificationMode::kUpdate);
  }
  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
