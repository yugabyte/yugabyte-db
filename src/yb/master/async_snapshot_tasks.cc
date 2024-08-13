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

#include "yb/master/async_snapshot_tasks.h"

#include "yb/consensus/consensus_error.h"

#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/ts_descriptor.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/backup.proxy.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"

DEFINE_test_flag(
    bool, simulate_long_restore, false,
    "Simulate a long restore failing to transition task to completion state if successful, thereby "
    "constantly retrying the RESTORE_ON_TABLET operation.");

DEFINE_test_flag(bool, pause_issuing_tserver_snapshot_requests, false,
                 "Whether to halt before issuing snapshot operations to tservers.");

namespace yb {
namespace master {

using std::string;
using tserver::TabletServerErrorPB;

////////////////////////////////////////////////////////////
// AsyncTabletSnapshotOp
////////////////////////////////////////////////////////////

namespace {

std::string SnapshotIdToString(const std::string& snapshot_id) {
  auto uuid = TryFullyDecodeTxnSnapshotId(snapshot_id);
  return uuid.IsNil() ? snapshot_id : uuid.ToString();
}

}

AsyncTabletSnapshotOp::AsyncTabletSnapshotOp(
    Master* master,
    ThreadPool* callback_pool,
    const TabletInfoPtr& tablet,
    const string& snapshot_id,
    tserver::TabletSnapshotOpRequestPB::Operation op,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<PickLeaderReplica>(tablet),
          tablet->table(),
          std::move(epoch),
          /* async_task_throttler */ nullptr),
      tablet_(tablet),
      snapshot_id_(snapshot_id),
      operation_(op) {}

string AsyncTabletSnapshotOp::description() const {
  return Format("$0 Tablet Snapshot Operation $1 RPC $2",
                *tablet_, tserver::TabletSnapshotOpRequestPB::Operation_Name(operation_),
                SnapshotIdToString(snapshot_id_));
}

TabletId AsyncTabletSnapshotOp::tablet_id() const {
  return tablet_->tablet_id();
}

TabletServerId AsyncTabletSnapshotOp::permanent_uuid() const {
  return target_ts_desc_ != nullptr ? target_ts_desc_->id() : "";
}

bool AsyncTabletSnapshotOp::RetryAllowed(TabletServerErrorPB::Code code, const Status& status) {
  switch (code) {
    case TabletServerErrorPB::TABLET_NOT_FOUND:
      return false;
    case TabletServerErrorPB::INVALID_SNAPSHOT:
      return operation_ != tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET;
    default:
      if (operation_ == tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET) {
        // First check if the restore is in a FAILED state.
        ListSnapshotRestorationsRequestPB req;
        ListSnapshotRestorationsResponsePB resp;
        req.set_restoration_id(restoration_id_.data(), restoration_id_.size());

        auto s = master_->catalog_manager()->ListSnapshotRestorations(&req, &resp);
        if (s.ok() && resp.restorations_size() == 1) {
          auto restoration = *resp.restorations().begin();
          if (restoration.entry().state() == SysSnapshotEntryPB::FAILED) {
            return false;
          }
        }
      }

      // Otherwise check status errors;
      return TransactionError(status) != TransactionErrorCode::kSnapshotTooOld &&
             consensus::ConsensusError(status) != consensus::ConsensusErrorPB::TABLET_SPLIT;
  }
}

void AsyncTabletSnapshotOp::HandleResponse(int attempt) {
  server::UpdateClock(resp_, master_->clock());

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    if (!RetryAllowed(resp_.error().code(), status)) {
      LOG_WITH_PREFIX(WARNING) << "Failed, NO retry: " << status;
      TransitionToCompleteState();
    } else {
      LOG_WITH_PREFIX(WARNING) << "Failed, will be retried: " << status;
    }
  } else {
    auto transit = !FLAGS_TEST_simulate_long_restore ||
                   operation_ != tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET;
    if (transit) {
      TransitionToCompleteState();
    }
    VLOG_WITH_PREFIX(1) << "Complete";
  }

  if (state() != server::MonitoredTaskState::kComplete) {
    VLOG_WITH_PREFIX(1) << "TabletSnapshotOp task is not completed";
  }
}

bool AsyncTabletSnapshotOp::SendRequest(int attempt) {
  TEST_PAUSE_IF_FLAG(TEST_pause_issuing_tserver_snapshot_requests);
  tserver::TabletSnapshotOpRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.add_tablet_id(tablet_->tablet_id());
  req.set_snapshot_id(snapshot_id_);
  req.set_operation(operation_);
  if (snapshot_schedule_id_) {
    req.set_schedule_id(snapshot_schedule_id_.data(), snapshot_schedule_id_.size());
  }
  if (restoration_id_) {
    req.set_restoration_id(restoration_id_.data(), restoration_id_.size());
  }
  if (snapshot_hybrid_time_) {
    req.set_snapshot_hybrid_time(snapshot_hybrid_time_.ToUint64());
  }
  if (restoration_hybrid_time_) {
    req.set_restoration_hybrid_time(restoration_hybrid_time_.ToUint64());
  }
  if (has_metadata_) {
    req.set_schema_version(schema_version_);
    *req.mutable_schema() = schema_;
    *req.mutable_indexes() = indexes_;
    req.set_hide(hide_);
  }

  *req.mutable_colocated_tables_metadata() = colocated_tables_metadata_;

  if (db_oid_) {
    req.set_db_oid(*db_oid_);
  }
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());

  ts_backup_proxy_->TabletSnapshotOpAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Sent to " << permanent_uuid() << " (attempt " << attempt << "): "
                      << (VLOG_IS_ON(4) ? req.ShortDebugString() : "");
  return true;
}

std::optional<std::pair<server::MonitoredTaskState, Status>>
AsyncTabletSnapshotOp::HandleReplicaLookupFailure(const Status& replica_lookup_status) {
  // Deleting an already deleted tablet snapshot
  if (replica_lookup_status.IsNotFound() &&
      operation_ == tserver::TabletSnapshotOpRequestPB::DELETE_ON_TABLET) {
    return std::make_pair(server::MonitoredTaskState::kComplete, Status::OK());
  }
  return std::nullopt;
}

void AsyncTabletSnapshotOp::Finished(const Status& status) {
  if (!callback_) {
    return;
  }
  if (!status.ok()) {
    callback_(status);
    return;
  }
  if (resp_.has_error()) {
    auto status = tablet_->CheckRunning();
    if (status.ok()) {
      status = StatusFromPB(resp_.error().status());
    }
    callback_(status);
  } else {
    callback_(&resp_);
  }
}

void AsyncTabletSnapshotOp::SetMetadata(const SysTablesEntryPB& pb) {
  has_metadata_ = true;
  schema_version_ = pb.version();
  schema_ = pb.schema();
  indexes_ = pb.indexes();
}

void AsyncTabletSnapshotOp::SetColocatedTableMetadata(
    const TableId& table_id, const SysTablesEntryPB& pb) {
  auto* metadata = colocated_tables_metadata_.Add();
  metadata->set_schema_version(pb.version());
  *metadata->mutable_schema() = pb.schema();
  *metadata->mutable_indexes() = pb.indexes();
  metadata->set_table_id(table_id);
}

} // namespace master
} // namespace yb
