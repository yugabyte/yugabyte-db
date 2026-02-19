// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/async_rpc_tasks.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_meta.h"

#include "yb/master/async_rpc_tasks_base.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_test.proxy.h"
#include "yb/master/ts_manager.h"
#include "yb/master/ts_descriptor.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/sync_point.h"

using namespace std::literals;

DEFINE_test_flag(bool, stuck_add_tablet_to_table_task_enabled, false, "description");

DEFINE_test_flag(bool, fail_async_delete_replica_task, false,
                 "When set, transition all delete replica tasks to a failed state.");

DEFINE_test_flag(string, skip_async_insert_packed_schema_for_tablet_id, "",
    "Tablet ID to skip AsyncInsertPackedSchemaForXClusterTarget requests for.");

DECLARE_int32(tablet_creation_timeout_ms);
DECLARE_int32(TEST_slowdown_alter_table_rpcs_ms);

DECLARE_bool(ysql_yb_enable_alter_table_rewrite);

namespace yb::master {

using namespace std::placeholders;

using std::string;
using std::shared_ptr;
using std::vector;

using strings::Substitute;
using consensus::RaftPeerPB;
using server::MonitoredTaskState;
using server::MonitoredTaskType;
using tserver::TabletServerErrorPB;


// ============================================================================
//  Class AsyncCreateReplica.
// ============================================================================
AsyncCreateReplica::AsyncCreateReplica(Master *master,
                                       ThreadPool *callback_pool,
                                       const string& permanent_uuid,
                                       const TabletInfoPtr& tablet,
                                       const TabletInfo::ReadLock& tablet_lock,
                                       const std::vector<SnapshotScheduleId>& snapshot_schedules,
                                       LeaderEpoch epoch,
                                       CDCSDKSetRetentionBarriers cdc_sdk_set_retention_barriers)
  : RetrySpecificTSRpcTaskWithTable(master, callback_pool, permanent_uuid, tablet->table(),
                           std::move(epoch), /* async_task_throttler */ nullptr),
    tablet_id_(tablet->tablet_id()),
    cdc_sdk_set_retention_barriers_(cdc_sdk_set_retention_barriers) {
  // May not honor unresponsive deadline, refer to UnresponsiveDeadline().
  deadline_ = start_timestamp_;
  deadline_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_tablet_creation_timeout_ms));

  auto table_lock = tablet->table()->LockForRead();
  const auto& table_pb = table_lock->pb;
  const auto& tablet_pb = tablet_lock->pb;

  req_.set_dest_uuid(permanent_uuid);
  req_.set_table_id(tablet->table()->id());
  req_.set_tablet_id(tablet->tablet_id());
  req_.set_table_type(tablet->table()->metadata().state().pb.table_type());
  req_.mutable_partition()->CopyFrom(tablet_pb.partition());
  req_.set_namespace_id(table_pb.namespace_id());
  req_.set_namespace_name(table_pb.namespace_name());
  req_.set_pg_table_id(table_pb.pg_table_id());
  req_.set_table_name(table_pb.name());
  req_.mutable_schema()->CopyFrom(table_pb.schema());
  req_.mutable_partition_schema()->CopyFrom(table_pb.partition_schema());
  req_.mutable_config()->CopyFrom(tablet_pb.committed_consensus_state().config());
  req_.set_colocated(tablet_pb.colocated());
  if (table_pb.has_index_info()) {
    req_.mutable_index_info()->CopyFrom(table_pb.index_info());
  }
  if (table_pb.has_wal_retention_secs()) {
    req_.set_wal_retention_secs(table_pb.wal_retention_secs());
  }
  auto& req_schedules = *req_.mutable_snapshot_schedules();
  req_schedules.Reserve(narrow_cast<int>(snapshot_schedules.size()));
  for (const auto& id : snapshot_schedules) {
    req_schedules.Add()->assign(id.AsSlice().cdata(), id.size());
  }

  req_.mutable_hosted_stateful_services()->CopyFrom(table_pb.hosted_stateful_services());
  req_.set_cdc_sdk_set_retention_barriers(cdc_sdk_set_retention_barriers);
}

std::string AsyncCreateReplica::description() const {
  return Format("CreateTablet RPC for tablet $0 ($1) on TS=$2",
                tablet_id_, table_name(), permanent_uuid_);
}

void AsyncCreateReplica::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (s.IsAlreadyPresent()) {
      LOG_WITH_PREFIX(INFO) << "CreateTablet RPC for tablet " << tablet_id_
                            << " on TS " << permanent_uuid_ << " returned already present: "
                            << s;
      TransitionToCompleteState();
    } else {
      LOG_WITH_PREFIX(WARNING) << "CreateTablet RPC for tablet " << tablet_id_
                               << " on TS " << permanent_uuid_ << " failed: " << s;
    }

    return;
  }
  if (cdc_sdk_set_retention_barriers_) {
    if (!resp_.has_cdc_sdk_safe_op_id()) {
      LOG(WARNING) << "Response did not include cdcsdk_safe_op_id. Not inserting any rows into "
                   << "cdc_state table for tablet id: " << tablet_id();
      return;
    }
    auto status = master_->catalog_manager()->PopulateCDCStateTableOnNewTableCreation(
        table(), tablet_id_, OpId::FromPB(resp_.cdc_sdk_safe_op_id()));
    if (!status.ok()) {
      WARN_NOT_OK(
          status, Format(
                      "$0 failed while populating cdc_state table in "
                      "AsyncCreateReplica::HandleResponse.",
                      description()));
      return;
    }
  }

  TransitionToCompleteState();
  VLOG_WITH_PREFIX(1) << "TS " << permanent_uuid_ << ": complete on tablet " << tablet_id_;
}

bool AsyncCreateReplica::SendRequest(int attempt) {
  ts_admin_proxy_->CreateTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send create tablet request to " << permanent_uuid_ << ":\n"
                      << " (attempt " << attempt << "):\n"
                      << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncMasterTabletHealthTask.
// ============================================================================
AsyncMasterTabletHealthTask::AsyncMasterTabletHealthTask(
    Master* master,
    ThreadPool* callback_pool,
    consensus::RaftPeerPB&& peer,
    std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler)
    : RetryingMasterRpcTask(
          master, callback_pool, std::move(peer), /* async_task_throttler */ nullptr),
      cb_handler_{std::move(cb_handler)} {}

void AsyncMasterTabletHealthTask::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "AsyncMasterTabletHealthTask RPC on "
                               << peer_.ShortDebugString() << " failed: " << s;
    }
    return;
  }
  cb_handler_->ReportHealthCheck(resp_, peer_.permanent_uuid());
  TransitionToCompleteState();
}

std::string AsyncMasterTabletHealthTask::description() const {
  return "Check tablet health for tablets on master " + peer_.ShortDebugString();
}

bool AsyncMasterTabletHealthTask::SendRequest(int attempt) {
  master_cluster_proxy_->CheckMasterTabletHealthAsync(req_, &resp_, &rpc_, BindRpcCallback());
  return true;
}

// ============================================================================
//  Class AsyncTserverTabletHealthTask.
// ============================================================================
AsyncTserverTabletHealthTask::AsyncTserverTabletHealthTask(
    Master* master,
    ThreadPool* callback_pool,
    std::string permanent_uuid,
    std::vector<TabletId>&& tablets,
    std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler)
  : RetrySpecificTSRpcTask(
      master, callback_pool, std::move(permanent_uuid), /* async_task_throttler */ nullptr),
    cb_handler_{std::move(cb_handler)} {
  for (auto& tablet_id : tablets) {
    req_.add_tablet_ids(std::move(tablet_id));
  }
}

void AsyncTserverTabletHealthTask::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "CheckTserverTabletHealth RPC on TS "
                               << permanent_uuid_ << " failed: " << s;
    }
    return;
  }
  cb_handler_->ReportHealthCheck(resp_, permanent_uuid_);
  TransitionToCompleteState();
}

std::string AsyncTserverTabletHealthTask::description() const {
  return "Check tablet health for tablets on TS " + permanent_uuid_;
}

bool AsyncTserverTabletHealthTask::SendRequest(int attempt) {
  ts_proxy_->CheckTserverTabletHealthAsync(req_, &resp_, &rpc_, BindRpcCallback());
  return true;
}

// ============================================================================
//  Class AsyncStartElection.
// ============================================================================
AsyncStartElection::AsyncStartElection(Master *master,
                                       ThreadPool *callback_pool,
                                       const string& permanent_uuid,
                                       const TabletInfoPtr& tablet,
                                       bool initial_election,
                                       LeaderEpoch epoch)
  : RetrySpecificTSRpcTaskWithTable(master, callback_pool, permanent_uuid,
        tablet->table(), std::move(epoch), /* async_task_throttler */ nullptr),
    tablet_id_(tablet->tablet_id()) {
  // May not honor unresponsive deadline, refer to UnresponsiveDeadline().
  deadline_ = start_timestamp_;
  deadline_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_tablet_creation_timeout_ms));

  req_.set_dest_uuid(permanent_uuid_);
  req_.set_tablet_id(tablet_id_);
  req_.set_initial_election(initial_election);
}

void AsyncStartElection::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "RunLeaderElection RPC for tablet " << tablet_id_
                               << " on TS " << permanent_uuid_ << " failed: " << s;
    }

    return;
  }

  TransitionToCompleteState();
}

std::string AsyncStartElection::description() const {
  return Format("RunLeaderElection RPC for tablet $0 ($1) on TS=$2",
                tablet_id_, table_name(), permanent_uuid_);
}

bool AsyncStartElection::SendRequest(int attempt) {
  LOG_WITH_PREFIX(INFO) << Format(
      "Hinted Leader start election at $0 for tablet $1, attempt $2",
      permanent_uuid_, tablet_id_, attempt);
  consensus_proxy_->RunLeaderElectionAsync(req_, &resp_, &rpc_, BindRpcCallback());

  return true;
}

// ============================================================================
//  Class AsyncPrepareDeleteTransactionTablet.
// ============================================================================
AsyncPrepareDeleteTransactionTablet::AsyncPrepareDeleteTransactionTablet(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
    const scoped_refptr<TableInfo>& table, const TabletInfoPtr& tablet,
    const std::string& msg, HideOnly hide_only, LeaderEpoch epoch)
    : RetrySpecificTSRpcTaskWithTable(master, callback_pool, permanent_uuid, table,
                             std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet), msg_(msg), hide_only_(hide_only) {}

void AsyncPrepareDeleteTransactionTablet::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    TabletServerErrorPB::Code code = resp_.error().code();
    switch (code) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": prepare delete failed for tablet "
            << tablet_id() << " because the tablet was not found. No further retry: "
            << status.ToString();
        TransitionToCompleteState();
        break;
      case TabletServerErrorPB::WRONG_SERVER_UUID:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": prepare delete failed for tablet "
            << tablet_id() << " due to an incorrect UUID. No further retry: "
            << status.ToString();
        TransitionToCompleteState();
        break;
      default:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": prepare delete failed for tablet "
            << tablet_id() << " with error code " << TabletServerErrorPB::Code_Name(code)
            << ": " << status.ToString();
        break;
    }
  } else {
    if (table_) {
      LOG_WITH_PREFIX(INFO)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id()
          << " (table " << table_->ToString() << ") successfully done";
    } else {
      LOG_WITH_PREFIX(WARNING)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id()
          << " did not belong to a known table, but was prepared for deletion";
    }
    TransitionToCompleteState();
    VLOG_WITH_PREFIX(1) << "TS " << permanent_uuid_ << ": complete on tablet " << tablet_id();
  }
}

std::string AsyncPrepareDeleteTransactionTablet::description() const {
  return Format("PrepareDeleteTransactionTablet RPC for tablet $0 ($1) on TS=$2",
                tablet_id(), table_name(), permanent_uuid_);
}

TabletId AsyncPrepareDeleteTransactionTablet::tablet_id() const {
  return tablet_->tablet_id();
}

bool AsyncPrepareDeleteTransactionTablet::SendRequest(int attempt) {
  tserver::PrepareDeleteTransactionTabletRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_tablet_id(tablet_id());

  ts_admin_proxy_->PrepareDeleteTransactionTabletAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send prepare delete transaction tablet request for "
                      << tablet_id() << " to " << permanent_uuid_
                      << " (attempt " << attempt << "):\n"
                      << req.DebugString();
  return true;
}

void AsyncPrepareDeleteTransactionTablet::UnregisterAsyncTaskCallback() {
  // Only notify if we are in a success state.
  if (state() == MonitoredTaskState::kComplete) {
    master_->catalog_manager()->NotifyPrepareDeleteTransactionTabletFinished(
        tablet_, msg_, hide_only_, epoch());
  }
}

// ============================================================================
//  Class AsyncDeleteReplica.
// ============================================================================
Status AsyncDeleteReplica::SetPendingDelete(AddPendingDelete add_pending_delete) {
  auto ts_desc = VERIFY_RESULT(master_->ts_manager()->LookupTSByUUID(permanent_uuid_));

  if (add_pending_delete) {
    ts_desc->AddPendingTabletDelete(tablet_id());
  } else {
    ts_desc->ClearPendingTabletDelete(tablet_id());
  }
  return Status::OK();
}

Status AsyncDeleteReplica::BeforeSubmitToTaskPool() {
  return SetPendingDelete(AddPendingDelete::kTrue);
}

Status AsyncDeleteReplica::OnSubmitFailure() {
  return SetPendingDelete(AddPendingDelete::kFalse);
}

void AsyncDeleteReplica::HandleResponse(int attempt) {
  if (FLAGS_TEST_fail_async_delete_replica_task) {
    auto s = STATUS(IllegalState, "TEST_fail_async_delete_replica_task set to true");
    TransitionToFailedState(MonitoredTaskState::kRunning, s);
    return;
  }

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    TabletServerErrorPB::Code code = resp_.error().code();
    switch (code) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " because the tablet was not found. No further retry: "
            << status.ToString();
        TransitionToCompleteState();
        break;
      case TabletServerErrorPB::CAS_FAILED:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " due to a CAS failure. No further retry: " << status.ToString();
        TransitionToCompleteState();
        break;
      case TabletServerErrorPB::WRONG_SERVER_UUID:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " due to an incorrect UUID. No further retry: " << status.ToString();
        TransitionToCompleteState();
        break;
      default:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " with error code " << TabletServerErrorPB::Code_Name(code)
            << ": " << status.ToString();
        break;
    }
  } else {
    if (table_) {
      LOG_WITH_PREFIX(INFO)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id_
          << " (table " << table_->ToString() << ") successfully done";
    } else {
      LOG_WITH_PREFIX(WARNING)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id_
          << " did not belong to a known table, but was successfully deleted";
    }
    TransitionToCompleteState();
    VLOG_WITH_PREFIX(1) << "TS " << permanent_uuid_ << ": complete on tablet " << tablet_id_;
  }
}

std::string AsyncDeleteReplica::description() const {
  return Format("$0Tablet RPC for tablet $1 ($2) on TS=$3",
                hide_only_ ? "Hide" : "Delete", tablet_id_, table_name(), permanent_uuid_);
}

bool AsyncDeleteReplica::SendRequest(int attempt) {
  tserver::DeleteTabletRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_tablet_id(tablet_id_);
  req.set_reason(reason_);
  req.set_delete_type(delete_type_);
  if (hide_only_) {
    req.set_hide_only(hide_only_);
  }
  if (keep_data_) {
    req.set_keep_data(keep_data_);
  }
  if (cas_config_opid_index_less_or_equal_) {
    req.set_cas_config_opid_index_less_or_equal(*cas_config_opid_index_less_or_equal_);
  }
  bool should_abort_active_txns = !table() ||
                                  table()->LockForRead()->started_deleting();
  req.set_should_abort_active_txns(should_abort_active_txns);
  if (should_abort_active_txns && exclude_aborting_transaction_id_.has_value()) {
    req.set_transaction_id(exclude_aborting_transaction_id_->ToString());
  }

  ts_admin_proxy_->DeleteTabletAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send delete tablet request to " << permanent_uuid_
                      << " (attempt " << attempt << "):\n"
                      << req.DebugString();
  return true;
}

void AsyncDeleteReplica::UnregisterAsyncTaskCallback() {
  // Only notify if we are in a success state.
  if (state() == MonitoredTaskState::kComplete || state() == MonitoredTaskState::kFailed) {
    master_->catalog_manager()->NotifyTabletDeleteFinished(
        permanent_uuid_, tablet_id_, table(), epoch(), state());
  }
}

bool AsyncDeleteReplica::RetryTaskAfterRPCFailure(const Status& status) {
  auto target_ts = target_ts_desc();
  if (!target_ts->IsLive()) {
    LOG_WITH_PREFIX(WARNING) << "TS " << target_ts->id() << ": delete failed for tablet "
                             << tablet_id() << ". TS is DEAD. No further retry.";
    return false;
  }
  return true;
}

// ============================================================================
//  Class AsyncAlterTable.
// ============================================================================
void AsyncAlterTable::HandleResponse(int attempt) {
  if (PREDICT_FALSE(FLAGS_TEST_slowdown_alter_table_rpcs_ms > 0)) {
    VLOG_WITH_PREFIX(1) << "Sleeping for " << tablet_->tablet_id()
                        << FLAGS_TEST_slowdown_alter_table_rpcs_ms
                        << "ms before returning response in async alter table request handler";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_alter_table_rpcs_ms));
  }

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    LOG_WITH_PREFIX(WARNING) << "TS " << permanent_uuid() << " failed: "
                             << status << " for version " << schema_version_;

    // Do not retry on a fatal error
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
      case TabletServerErrorPB::MISMATCHED_SCHEMA:
      case TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA:
        TransitionToCompleteState();
        break;
      default:
        break;
    }
  } else {
    // CDC SDK Create Stream Context
    // Technically, handling the CDCSDK snapshot flow is part of AlterTable processing. So it is
    // done before transitioning to complete state.
    // If there is an error while populating the cdc_state table, it can be ignored here
    // as it will be handled in CatalogManager::CreateNewXReplStream
    if (cdc_sdk_stream_id_) {
      auto sync_point_tablet = tablet_id();
      TEST_SYNC_POINT_CALLBACK("AsyncAlterTable::CDCSDKCreateStream", &sync_point_tablet);

      if (resp_.has_cdc_sdk_snapshot_safe_op_id() && resp_.has_propagated_hybrid_time()) {
        WARN_NOT_OK(
            master_->catalog_manager()->PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails(
                table(), tablet_id(), cdc_sdk_stream_id_, resp_.cdc_sdk_snapshot_safe_op_id(),
                HybridTime::FromPB(resp_.propagated_hybrid_time()),
                cdc_sdk_require_history_cutoff_),
            Format(
              "$0 failed while populating cdc_state table in AsyncAlterTable::HandleResponse. "
              "Response $1", description(),
              resp_.ShortDebugString()));
      } else {
        LOG(WARNING) << "Response not as expected. Not inserting any rows into "
                     << "cdc_state table for stream_id: " << cdc_sdk_stream_id_
                     << " and tablet id: " << tablet_id();
      }
    }

    TransitionToCompleteState();
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << " completed: for version " << schema_version_;
  }

  server::UpdateClock(resp_, master_->clock());

  if (state() == MonitoredTaskState::kComplete) {
    // TODO: proper error handling here. Not critical, since TSHeartbeat will retry on failure.
    WARN_NOT_OK(
        master_->catalog_manager()->HandleTabletSchemaVersionReport(
            tablet_.get(), schema_version_, epoch(), table()),
        Format(
            "$0 failed while running AsyncAlterTable::HandleResponse. Response $1", description(),
            resp_.ShortDebugString()));
  } else {
    VLOG_WITH_PREFIX(1) << "Task is not completed " << tablet_->ToString() << " for version "
                        << schema_version_;
  }
}

TableType AsyncAlterTable::table_type() const {
  return tablet_->table()->GetTableType();
}

bool AsyncAlterTable::SendRequest(int attempt) {
  VLOG_WITH_PREFIX(1) << "Send alter table request to " << permanent_uuid() << " for "
                      << tablet_->tablet_id() << " waiting for a read lock.";

  tablet::ChangeMetadataRequestPB req;
  {
    auto l = table_->LockForRead();
    VLOG_WITH_PREFIX(1) << "Send alter table request to " << permanent_uuid() << " for "
                        << tablet_->tablet_id() << " obtained the read lock.";

    req.set_schema_version(l->pb.version());
    req.set_dest_uuid(permanent_uuid());
    req.set_tablet_id(tablet_->tablet_id());
    req.set_alter_table_id(table_->id());

    if (l->pb.has_wal_retention_secs()) {
      req.set_wal_retention_secs(l->pb.wal_retention_secs());
    }

    // Support for CDC SDK Create Stream context
    if (cdc_sdk_stream_id_) {
      req.set_retention_requester_id(cdc_sdk_stream_id_.ToString());
      req.set_cdc_sdk_require_history_cutoff(cdc_sdk_require_history_cutoff_);
    }

    req.mutable_schema()->CopyFrom(l->pb.schema());
    req.set_new_table_name(l->pb.name());
    req.mutable_indexes()->CopyFrom(l->pb.indexes());
    req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());

    // First provisional column ID is the earliest column ID we can set next_column_id to.
    int32_t first_provisional_column_id = l->pb.next_column_id();
    for (const auto& state : l->pb.ysql_ddl_txn_verifier_state()) {
      if (state.has_previous_next_column_id()) {
        // If we rollback, we will move next_column_id back to this.
        first_provisional_column_id = state.previous_next_column_id();
        break;
      }
    }
    req.set_first_provisional_column_id(first_provisional_column_id);

    if (l->pb.has_transaction() && l->pb.transaction().has_using_table_locks() &&
        l->pb.transaction().using_table_locks()) {
      req.set_only_abort_txns_not_using_table_locks(true);
    }

    if (table_type() == TableType::PGSQL_TABLE_TYPE && !transaction_id_.IsNil()) {
      VLOG_WITH_PREFIX(1) << "Transaction ID is provided for tablet " << tablet_->tablet_id()
                          << " with ID " << transaction_id_.ToString()
                          << " for ALTER TABLE operation";
      req.set_should_abort_active_txns(true);
      req.set_transaction_id(transaction_id_.ToString());
    }

    schema_version_ = l->pb.version();

    HandleInsertPackedSchema(req);
  }

  ts_admin_proxy_->AlterSchemaAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Send alter table request to " << permanent_uuid() << " for " << tablet_->tablet_id()
      << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

bool AsyncBackfillDone::SendRequest(int attempt) {
  VLOG_WITH_PREFIX(1)
      << "Send alter table request to " << permanent_uuid() << " for " << tablet_->tablet_id()
      << " version " << schema_version_ << " waiting for a read lock.";

  tablet::ChangeMetadataRequestPB req;
  {
    auto l = table_->LockForRead();
    VLOG_WITH_PREFIX(1)
        << "Send alter table request to " << permanent_uuid() << " for " << tablet_->tablet_id()
        << " version " << schema_version_ << " obtained the read lock.";

    req.set_backfill_done_table_id(table_id_);
    req.set_dest_uuid(permanent_uuid());
    req.set_tablet_id(tablet_->tablet_id());
    req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
    req.set_mark_backfill_done(true);
    schema_version_ = l->pb.version();
  }

  ts_admin_proxy_->BackfillDoneAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Send backfill done request to " << permanent_uuid() << " for " << tablet_->tablet_id()
      << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

void AsyncInsertPackedSchemaForXClusterTarget::HandleInsertPackedSchema(
    tablet::ChangeMetadataRequestPB& req) {
  // Update insert_packed_schema and update the schema to the packed schema to insert.
  // This schema will get inserted into the historical packing schemas with [schema_version - 1],
  // and then the current schema will be reinserted with [schema_version].
  req.set_insert_packed_schema(true);
  req.mutable_schema()->CopyFrom(packed_schema_);
}

bool AsyncInsertPackedSchemaForXClusterTarget::SendRequest(int attempt) {
  if (PREDICT_FALSE(!FLAGS_TEST_skip_async_insert_packed_schema_for_tablet_id.empty())) {
    if (tablet_id() == FLAGS_TEST_skip_async_insert_packed_schema_for_tablet_id) {
      LOG_WITH_PREFIX(INFO) << "Skipping AsyncInsertPackedSchemaForXClusterTarget for tablet "
                            << tablet_id()
                            << " due to FLAGS_TEST_skip_async_insert_packed_schema_for_tablet_id";
      TransitionToCompleteState();
      // Need to unregister the task here since we don't follow the regular RetryingTSRpcTask flow.
      UnregisterAsyncTask();
      return true;
    }
  }
  return AsyncAlterTable::SendRequest(attempt);
}

void AsyncGetLatestCompatibleSchemaVersion::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    callback_(StatusFromPB(resp_.error().status()), 0);
    TransitionToFailedState(
        server::MonitoredTaskState::kRunning, StatusFromPB(resp_.error().status()));
    return;
  }

  if (resp_.has_compatible_schema_version()) {
    callback_(Status::OK(), resp_.compatible_schema_version());
    TransitionToCompleteState();
  } else {
    auto status = STATUS(IllegalState, "Missing compatible_schema_version in response");
    callback_(status, 0);
    TransitionToFailedState(server::MonitoredTaskState::kRunning, status);
  }
}

bool AsyncGetLatestCompatibleSchemaVersion::SendRequest(int attempt) {
  req_.set_tablet_id(tablet_id());
  req_.mutable_schema()->CopyFrom(schema_);

  ts_proxy_->GetCompatibleSchemaVersionAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Sent get compatible schema version request to " << permanent_uuid()
                      << " (attempt " << attempt << "):\n"
                      << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncTruncate.
// ============================================================================
void AsyncTruncate::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const Status s = StatusFromPB(resp_.error().status());
    const TabletServerErrorPB::Code code = resp_.error().code();
    LOG_WITH_PREFIX(WARNING)
        << "TS " << permanent_uuid() << ": truncate failed for tablet " << tablet_id()
        << " with error code " << TabletServerErrorPB::Code_Name(code) << ": " << s;
  } else {
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << ": truncate complete on tablet " << tablet_id();
    TransitionToCompleteState();
  }

  server::UpdateClock(resp_, master_->clock());
}

bool AsyncTruncate::SendRequest(int attempt) {
  tserver::TruncateRequestPB req;
  req.set_tablet_id(tablet_id());
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_proxy_->TruncateAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send truncate tablet request to " << permanent_uuid()
                      << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

// ============================================================================
//  Class CommonInfoForRaftTask.
// ============================================================================
CommonInfoForRaftTask::CommonInfoForRaftTask(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)),
          tablet->table(), std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet),
      cstate_(cstate),
      change_config_ts_uuid_(change_config_ts_uuid) {
  // No deadline for the task, refer to ComputeDeadline() for a single attempt deadline.
  deadline_ = MonoTime::Max();
}

CommonInfoForRaftTask::~CommonInfoForRaftTask() = default;

TabletId CommonInfoForRaftTask::tablet_id() const {
  return tablet_->tablet_id();
}

// ============================================================================
//  Class AsyncChangeConfigTask.
// ============================================================================
std::string AsyncChangeConfigTask::description() const {
  return Format(
      "$0 RPC for tablet $1 ($2) on peer $3 with cas_config_opid_index $4. Reason: $5", type_name(),
      tablet_->tablet_id(), table_name(), permanent_uuid(), cstate_.config().opid_index(), reason_);
}

bool AsyncChangeConfigTask::SendRequest(int attempt) {
  // Bail if we're retrying in vain.
  int64_t latest_index;
  {
    auto tablet_lock = tablet_->LockForRead();
    latest_index = tablet_lock->pb.committed_consensus_state().config().opid_index();
    // Adding this logic for a race condition that occurs in this scenario:
    // 1. CatalogManager receives a DeleteTable request and sends DeleteTablet requests to the
    // tservers, but doesn't yet update the tablet in memory state to not running.
    // 2. The CB runs and sees that this tablet is still running, sees that it is over-replicated
    // (since the placement now dictates it should have 0 replicas),
    // but before it can send the ChangeConfig RPC to a tserver.
    // 3. That tserver processes the DeleteTablet request.
    // 4. The ChangeConfig RPC now returns tablet not found,
    // which prompts an infinite retry of the RPC.
    bool tablet_running = tablet_lock->is_running();
    if (!tablet_running) {
      AbortTask(STATUS(Aborted, "Tablet is not running"));
      return false;
    }
  }
  if (latest_index > cstate_.config().opid_index()) {
    auto status = STATUS_FORMAT(
        Aborted,
        "Latest config for has opid_index of $0 while this task has opid_index of $1",
        latest_index, cstate_.config().opid_index());
    LOG_WITH_PREFIX(INFO) << status;
    AbortTask(status);
    return false;
  }

  // Logging should be covered inside based on failure reasons.
  auto prepare_status = PrepareRequest(attempt);
  if (!prepare_status.ok()) {
    AbortTask(prepare_status);
    return false;
  }

  consensus_proxy_->ChangeConfigAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Task " << description() << " sent request:\n" << req_.DebugString();
  return true;
}

void AsyncChangeConfigTask::HandleResponse(int attempt) {
  if (!resp_.has_error()) {
    TransitionToCompleteState();
    LOG_WITH_PREFIX(INFO) << Format(
        "Change config succeeded on leader TS $0 for tablet $1 with type $2 for replica $3",
        permanent_uuid(), tablet_->tablet_id(), type_name(), change_config_ts_uuid_);
    return;
  }

  Status status = StatusFromPB(resp_.error().status());

  // Do not retry on some known errors, otherwise retry forever or until cancelled.
  switch (resp_.error().code()) {
    case TabletServerErrorPB::CAS_FAILED:
    case TabletServerErrorPB::ADD_CHANGE_CONFIG_ALREADY_PRESENT:
    case TabletServerErrorPB::REMOVE_CHANGE_CONFIG_NOT_PRESENT:
    case TabletServerErrorPB::NOT_THE_LEADER:
      LOG_WITH_PREFIX(WARNING) << "ChangeConfig() failed on leader " << permanent_uuid()
                               << ". No further retry: " << status.ToString();
      TransitionToCompleteState();
      break;
    default:
      LOG_WITH_PREFIX(INFO) << "ChangeConfig() failed on leader " << permanent_uuid()
                            << " due to error "
                            << TabletServerErrorPB::Code_Name(resp_.error().code())
                            << ". This operation will be retried. Error detail: "
                            << status.ToString();
      break;
  }
}

// ============================================================================
//  Class AsyncAddServerTask.
// ============================================================================
Status AsyncAddServerTask::PrepareRequest(int attempt) {
  // Select the replica we wish to add to the config.
  // Do not include current members of the config.
  std::unordered_set<string> replica_uuids;
  for (const RaftPeerPB& peer : cstate_.config().peers()) {
    InsertOrDie(&replica_uuids, peer.permanent_uuid());
  }
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  shared_ptr<TSDescriptor> replacement_replica;
  for (auto ts_desc : ts_descs) {
    if (ts_desc->permanent_uuid() == change_config_ts_uuid_) {
      // This is given by the client, so we assume it is a well chosen uuid.
      replacement_replica = ts_desc;
      break;
    }
  }
  if (PREDICT_FALSE(!replacement_replica)) {
    auto status = STATUS_FORMAT(
        TimedOut, "Could not find desired replica $0 in live set", change_config_ts_uuid_);
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  req_.set_dest_uuid(permanent_uuid());
  req_.set_tablet_id(tablet_->tablet_id());
  req_.set_type(consensus::ADD_SERVER);
  req_.set_cas_config_opid_index(cstate_.config().opid_index());
  RaftPeerPB* peer = req_.mutable_server();
  peer->set_permanent_uuid(replacement_replica->permanent_uuid());
  peer->set_member_type(member_type_);
  auto peer_reg = replacement_replica->GetRegistration();

  if (peer_reg.private_rpc_addresses().empty()) {
    auto status = STATUS_FORMAT(
        IllegalState, "Candidate replacement $0 has no registered rpc address: $1",
        replacement_replica->permanent_uuid(), peer_reg);
    YB_LOG_EVERY_N(WARNING, 100) << LogPrefix() << status;
    return status;
  }

  TakeRegistration(&peer_reg, peer);

  return Status::OK();
}

// ============================================================================
//  Class AsyncRemoveServerTask.
// ============================================================================
Status AsyncRemoveServerTask::PrepareRequest(int attempt) {
  bool found = false;
  for (const RaftPeerPB& peer : cstate_.config().peers()) {
    if (change_config_ts_uuid_ == peer.permanent_uuid()) {
      found = true;
    }
  }

  if (!found) {
    auto status = STATUS_FORMAT(
        NotFound, "Asked to remove TS with uuid $0 but could not find it in config peers!",
        change_config_ts_uuid_);
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  req_.set_dest_uuid(permanent_uuid());
  req_.set_tablet_id(tablet_->tablet_id());
  req_.set_type(consensus::REMOVE_SERVER);
  req_.set_cas_config_opid_index(cstate_.config().opid_index());
  RaftPeerPB* peer = req_.mutable_server();
  peer->set_permanent_uuid(change_config_ts_uuid_);

  return Status::OK();
}

// ============================================================================
//  Class AsyncTryStepDown.
// ============================================================================
std::string AsyncTryStepDown::description() const {
  return Format(
      "$0 RPC for tablet $1 ($2) on peer $3. Reason: $4", type_name(), tablet_->tablet_id(),
      table_name(), permanent_uuid(), reason_);
}

Status AsyncTryStepDown::PrepareRequest(int attempt) {
  LOG_WITH_PREFIX(INFO) << Substitute("Prep Leader step down $0, leader_uuid=$1, change_ts_uuid=$2",
                                      attempt, permanent_uuid(), change_config_ts_uuid_);
  if (attempt > 1) {
    return STATUS(RuntimeError, "Retry is not allowed");
  }

  // If we were asked to remove the server even if it is the leader, we have to call StepDown, but
  // only if our current leader is the server we are asked to remove.
  if (permanent_uuid() != change_config_ts_uuid_) {
    auto status = STATUS_FORMAT(
        IllegalState,
        "Incorrect state config leader $0 does not match target uuid $1 for a leader step down op",
        permanent_uuid(), change_config_ts_uuid_);
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  stepdown_req_.set_dest_uuid(change_config_ts_uuid_);
  stepdown_req_.set_tablet_id(tablet_->tablet_id());
  if (!new_leader_uuid_.empty()) {
    stepdown_req_.set_new_leader_uuid(new_leader_uuid_);
  }

  return Status::OK();
}

bool AsyncTryStepDown::SendRequest(int attempt) {
  auto prepare_status = PrepareRequest(attempt);
  if (!prepare_status.ok()) {
    AbortTask(prepare_status);
    return false;
  }

  LOG_WITH_PREFIX(INFO) << Format(
      "Stepping down leader $0 for tablet $1$2", change_config_ts_uuid_, tablet_->tablet_id(),
      new_leader_uuid_.empty() ? "" : " with new leader " + new_leader_uuid_);
  consensus_proxy_->LeaderStepDownAsync(
      stepdown_req_, &stepdown_resp_, &rpc_, BindRpcCallback());

  return true;
}

void AsyncTryStepDown::HandleResponse(int attempt) {
  if (!rpc_.status().ok()) {
    AbortTask(rpc_.status());
    LOG_WITH_PREFIX(WARNING) << Substitute(
        "Got error on stepdown for tablet $0 with leader $1, attempt $2 and error $3",
        tablet_->tablet_id(), permanent_uuid(), attempt, rpc_.status().ToString());

    return;
  }

  TransitionToCompleteState();
  const bool stepdown_failed = stepdown_resp_.has_error() &&
                               stepdown_resp_.error().status().code() != AppStatusPB::OK;
  LOG_WITH_PREFIX(INFO) << Format(
      "Leader step down done attempt=$0, leader_uuid=$1, change_uuid=$2, "
      "error=$3, failed=$4, should_remove=$5 for tablet $6.",
      attempt, permanent_uuid(), change_config_ts_uuid_, stepdown_resp_.error(),
      stepdown_failed, should_remove_, tablet_->tablet_id());

  if (stepdown_failed) {
    tablet_->RegisterLeaderStepDownFailure(change_config_ts_uuid_,
        MonoDelta::FromMilliseconds(stepdown_resp_.has_time_since_election_failure_ms() ?
                                    stepdown_resp_.time_since_election_failure_ms() : 0));
  }

  if (should_remove_) {
    auto task = std::make_shared<AsyncRemoveServerTask>(
        master_, callback_pool_, tablet_, cstate_, change_config_ts_uuid_, epoch(),
        Format("Done stepping down leader. Now removing replica as requested: $0", reason_));
    tablet_->table()->AddTask(task);
    Status status = task->Run();
    WARN_NOT_OK(status, "Failed to send new RemoveServer request");
  }
}

// ============================================================================
//  Class AsyncAddTableToTablet.
// ============================================================================
AsyncAddTableToTablet::AsyncAddTableToTablet(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    const scoped_refptr<TableInfo>& table, LeaderEpoch epoch,
    const std::shared_ptr<std::atomic<size_t>>& task_counter)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<PickLeaderReplica>(tablet), table.get(),
          std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet),
      tablet_id_(tablet->tablet_id()),
      task_counter_(task_counter) {
  req_.set_tablet_id(tablet->id());
  auto& add_table = *req_.mutable_add_table();
  add_table.set_table_id(table_->id());
  add_table.set_table_name(table_->name());
  add_table.set_table_type(table_->GetTableType());
  {
    auto l = table->LockForRead();
    add_table.set_schema_version(l->pb.version());
    *add_table.mutable_schema() = l->pb.schema();
    *add_table.mutable_partition_schema() = l->pb.partition_schema();
    if (l->pb.has_index_info()) {
      *add_table.mutable_index_info() = l->pb.index_info();
    }
    add_table.mutable_old_schema_packings()->CopyFrom(
        l->pb.xcluster_table_info().xcluster_colocated_old_schema_packings());
  }
  add_table.set_pg_table_id(table_->pg_table_id());
  add_table.set_skip_table_tombstone_check(FLAGS_ysql_yb_enable_alter_table_rewrite);
}

string AsyncAddTableToTablet::description() const {
  return Substitute("AddTableToTablet RPC ($0) ($1)", table_->ToString(), tablet_->ToString());
}

void AsyncAddTableToTablet::HandleResponse(int attempt) {
  if (!rpc_.status().ok()) {
    AbortTask(rpc_.status());
    LOG_WITH_PREFIX(WARNING) << Substitute(
        "Got error when adding table $0 to tablet $1, attempt $2 and error $3",
        table_->ToString(), tablet_->ToString(), attempt, rpc_.status().ToString());
    return;
  }
  if (resp_.has_error()) {
    LOG_WITH_PREFIX(WARNING) << "AddTableToTablet() responded with error code "
                             << TabletServerErrorPB_Code_Name(resp_.error().code());
    switch (resp_.error().code()) {
      case TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE: FALLTHROUGH_INTENDED;
      case TabletServerErrorPB::NOT_THE_LEADER:
        TransitionToWaitingState(MonitoredTaskState::kRunning);
        break;
      default:
        TransitionToCompleteState();
        break;
    }

    return;
  }

  auto tablets_running_result = table_->AreAllTabletsRunning();
  if (!tablets_running_result.ok()) {
    LOG(WARNING) << Format(
        "Error when handling response for AddTableToTablet RPC, cannot determine if all tablets "
        "are running for table $0",
        table_->id());
    TransitionToFailedState(MonitoredTaskState::kRunning, tablets_running_result.status());
    return;
  }
  LOG_IF(DFATAL, !*tablets_running_result)
      << "Not all tablets are running while processing AddTableToTablet response";
  if (--*task_counter_ == 0) {
    VLOG_WITH_FUNC(1) << "Marking table " << table_->ToString() << " as RUNNING";
    Status s = master_->catalog_manager()->PromoteTableToRunningState(table_, epoch());
    if (!s.ok()) {
      auto started_hiding_or_deleting = table_->LockForRead()->started_hiding_or_deleting();
      (started_hiding_or_deleting ? LOG(WARNING) : LOG(DFATAL))
          << "Error updating table " << table_->ToString() << ": " << s;
      TransitionToFailedState(MonitoredTaskState::kRunning, s);
      return;
    }
  }

  TransitionToCompleteState();
}

bool AsyncAddTableToTablet::SendRequest(int attempt) {
  if (PREDICT_FALSE(FLAGS_TEST_stuck_add_tablet_to_table_task_enabled)) {
    LOG_WITH_FUNC(WARNING) << "Causing the task to get stuck";
    return true;
  }

  ts_admin_proxy_->AddTableToTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Send AddTableToTablet request (attempt " << attempt << "):\n" << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncRemoveTableFromTablet.
// ============================================================================
AsyncRemoveTableFromTablet::AsyncRemoveTableFromTablet(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    const scoped_refptr<TableInfo>& table, LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<PickLeaderReplica>(tablet), table.get(),
          std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet),
      tablet_id_(tablet->tablet_id()) {
  req_.set_tablet_id(tablet->id());
  req_.set_remove_table_id(table->id());
}

string AsyncRemoveTableFromTablet::description() const {
  return Substitute("RemoveTableFromTablet RPC ($0) ($1)", table_->ToString(), tablet_->ToString());
}

void AsyncRemoveTableFromTablet::HandleResponse(int attempt) {
  if (!rpc_.status().ok()) {
    AbortTask(rpc_.status());
    LOG_WITH_PREFIX(WARNING) << Substitute(
        "Got error when removing table $0 from tablet $1, attempt $2 and error $3",
        table_->ToString(), tablet_->ToString(), attempt, rpc_.status().ToString());
    return;
  }
  if (resp_.has_error()) {
    LOG_WITH_PREFIX(WARNING)
        << "RemoveTableFromTablet responded with error: " << AsString(resp_.error());
    switch (resp_.error().code()) {
      case TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE: FALLTHROUGH_INTENDED;
      case TabletServerErrorPB::NOT_THE_LEADER:
        TransitionToWaitingState(MonitoredTaskState::kRunning);
        break;
      default:
        TransitionToCompleteState();
        break;
    }
  } else {
    TransitionToCompleteState();
  }
}

bool AsyncRemoveTableFromTablet::SendRequest(int attempt) {
  ts_admin_proxy_->RemoveTableFromTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send RemoveTableFromTablet request (attempt " << attempt << "):\n"
                      << req_.DebugString();
  return true;
}

namespace {

// These are errors that we are unlikely to recover from by retrying the GetSplitKey or SplitTablet
// RPC task. Automatic splits that receive these errors may still be retried in the next run, so we
// should try to not trigger splits that might hit these errors.
bool ShouldRetrySplitTabletRPC(const Status& s) {
  return !(s.IsInvalidArgument() || s.IsNotFound() || s.IsNotSupported() || s.IsIncomplete());
}

} // namespace

// ============================================================================
//  Class AsyncGetTabletSplitKey.
// ============================================================================
AsyncGetTabletSplitKey::AsyncGetTabletSplitKey(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    const ManualSplit is_manual_split, int split_factor, LeaderEpoch epoch,
    DataCallbackType result_cb)
  : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)), result_cb_(result_cb) {
  req_.set_tablet_id(tablet_id());
  req_.set_is_manual_split(is_manual_split);
  req_.set_split_factor(split_factor);
}

void AsyncGetTabletSplitKey::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const Status s = StatusFromPB(resp_.error().status());
    const TabletServerErrorPB::Code code = resp_.error().code();
    LOG_WITH_PREFIX(INFO) << "TS " << permanent_uuid() << ": GetSplitKey (attempt " << attempt
                          << ") failed for tablet " << tablet_id() << " with error code "
                          << TabletServerErrorPB::Code_Name(code) << ": " << s;
    if (!ShouldRetrySplitTabletRPC(s) ||
        (s.IsIllegalState() && code != tserver::TabletServerErrorPB::NOT_THE_LEADER)) {
      // It can happen that tablet leader has completed post-split compaction after previous split,
      // but followers have not yet completed post-split compaction.
      // Catalog manager decides to split again and sends GetTabletSplitKey RPC, but tablet leader
      // changes due to some reason and new tablet leader is not yet compacted.
      // In this case we get IllegalState error and we don't want to retry until post-split
      // compaction happened on leader. Once post-split compaction is done, CatalogManager will
      // resend RPC.
      //
      // Another case for IsIllegalState is trying to split a tablet that has all the data with
      // the same hash_code or the same doc_key, in this case we also don't want to retry RPC
      // automatically.
      // See https://github.com/yugabyte/yugabyte-db/issues/9159.
      TransitionToFailedState(state(), s);
    }
  } else {
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << ": got split key for tablet " << tablet_id();
    TransitionToCompleteState();
  }

  server::UpdateClock(resp_, master_->clock());
}

bool AsyncGetTabletSplitKey::SendRequest(int attempt) {
  req_.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_proxy_->GetSplitKeyAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Sent get split key request to " << permanent_uuid() << " (attempt " << attempt << "):\n"
      << req_.DebugString();
  return true;
}

void AsyncGetTabletSplitKey::Finished(const Status& status) {
  if (result_cb_) {
    if (status.ok()) {
      Data data;
      if (resp_.split_partition_keys_size() > 0) {
        DCHECK_EQ(resp_.split_partition_keys_size(), req_.split_factor() - 1);
        DCHECK_EQ(resp_.split_encoded_keys_size(), req_.split_factor() - 1);
        data.split_encoded_keys.assign(
            resp_.split_encoded_keys().begin(), resp_.split_encoded_keys().end());
        data.split_partition_keys.assign(
            resp_.split_partition_keys().begin(), resp_.split_partition_keys().end());
      } else {
        data.split_encoded_keys.push_back(resp_.deprecated_split_encoded_key());
        data.split_partition_keys.push_back(resp_.deprecated_split_partition_key());
      }

      result_cb_(data);
    } else {
      result_cb_(status);
    }
  }
}

// ============================================================================
//  Class AsyncSplitTablet.
// ============================================================================
AsyncSplitTablet::AsyncSplitTablet(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    const std::vector<TabletId>& new_tablet_ids,
    const std::vector<std::string>& split_encoded_keys,
    const std::vector<std::string>& split_partition_keys, LeaderEpoch epoch)
  : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)) {
  req_.set_tablet_id(tablet_id());
  DCHECK_EQ(new_tablet_ids.size() - 1, split_encoded_keys.size());
  DCHECK_EQ(new_tablet_ids.size() - 1, split_partition_keys.size());
  req_.set_deprecated_new_tablet1_id(new_tablet_ids[0]);
  req_.set_deprecated_new_tablet2_id(new_tablet_ids[1]);
  req_.set_deprecated_split_encoded_key(split_encoded_keys.front());
  req_.set_deprecated_split_partition_key(split_partition_keys.front());
  req_.mutable_new_tablet_ids()->Add(new_tablet_ids.begin(), new_tablet_ids.end());
  req_.mutable_split_encoded_keys()->Add(split_encoded_keys.begin(), split_encoded_keys.end());
  req_.mutable_split_partition_keys()->Add(
      split_partition_keys.begin(), split_partition_keys.end());
}

void AsyncSplitTablet::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const Status s = StatusFromPB(resp_.error().status());
    const TabletServerErrorPB::Code code = resp_.error().code();
    LOG_WITH_PREFIX(WARNING) << "TS " << permanent_uuid() << ": split (attempt " << attempt
                             << ") failed for tablet " << tablet_id() << " with error code "
                             << TabletServerErrorPB::Code_Name(code) << ": " << s;
    if (s.IsAlreadyPresent()) {
      TransitionToCompleteState();
    } else if (!ShouldRetrySplitTabletRPC(s)) {
      TransitionToFailedState(state(), s);
    }
  } else {
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << ": split complete on tablet " << tablet_id();
    TransitionToCompleteState();
  }

  server::UpdateClock(resp_, master_->clock());
}

bool AsyncSplitTablet::SendRequest(int attempt) {
  req_.set_dest_uuid(permanent_uuid());
  req_.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_admin_proxy_->SplitTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Sent split tablet request to " << permanent_uuid() << " (attempt " << attempt << "):\n"
      << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncUpdateTransactionTablesVersion.
// ============================================================================
AsyncUpdateTransactionTablesVersion::AsyncUpdateTransactionTablesVersion(
    Master* master,
    ThreadPool* callback_pool,
    const TabletServerId& ts_uuid,
    uint64_t version,
    StdStatusCallback callback)
    : RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      version_(version),
      callback_(std::move(callback)) {}

std::string AsyncUpdateTransactionTablesVersion::description() const {
  return "Update transaction tables version RPC";
}

void AsyncUpdateTransactionTablesVersion::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    LOG(WARNING) << "Updating transaction tables version on TS " << permanent_uuid_ << "failed: "
                 << status;
    return;
  }

  TransitionToCompleteState();
}

bool AsyncUpdateTransactionTablesVersion::SendRequest(int attempt) {
  tserver::UpdateTransactionTablesVersionRequestPB req;
  req.set_version(version_);
  ts_admin_proxy_->UpdateTransactionTablesVersionAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send transaction tables version update to " << permanent_uuid_;
  return true;
}

void AsyncUpdateTransactionTablesVersion::Finished(const Status& status) {
  callback_(status);
}

// ============================================================================
//  Class AsyncTsTestRetry.
// ============================================================================
AsyncTsTestRetry::AsyncTsTestRetry(
    Master* master,
    ThreadPool* callback_pool,
    const TabletServerId& ts_uuid,
    const int32_t num_retries,
    StdStatusCallback callback)
    : RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      num_retries_(num_retries),
      callback_(std::move(callback)) {}

string AsyncTsTestRetry::description() const {
  return Format("$0 TsTestRetry RPC", permanent_uuid_);
}

void AsyncTsTestRetry::HandleResponse(int attempt) {
  server::UpdateClock(resp_, master_->clock());

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    LOG(INFO) << "TEST: TS " << permanent_uuid_ << ": test retry failed: " << status.ToString();
    return;
  }

  callback_(Status::OK());
  TransitionToCompleteState();
}

bool AsyncTsTestRetry::SendRequest(int attempt) {
  tserver::TestRetryRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  req.set_num_retries(num_retries_);

  ts_admin_proxy_->TestRetryAsync(req, &resp_, &rpc_, BindRpcCallback());
  return true;
}


// ============================================================================
//  Class AsyncMasterTestRetry.
// ============================================================================
AsyncMasterTestRetry::AsyncMasterTestRetry(
    Master* master, ThreadPool* callback_pool, consensus::RaftPeerPB&& peer, int32_t num_retries,
    StdStatusCallback callback)
    : RetryingMasterRpcTask(master, callback_pool, std::move(peer)),
      num_retries_(num_retries),
      callback_(std::move(callback)) {}

std::string AsyncMasterTestRetry::description() const {
  return Format("$0 MasterTestRetry RPC", peer_.permanent_uuid());
}

void AsyncMasterTestRetry::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());
    LOG(INFO) << "AsyncMasterTestRetry failed: " << status.ToString();
    return;
  }

  callback_(Status::OK());
  TransitionToCompleteState();
}

bool AsyncMasterTestRetry::SendRequest(int attempt) {
  master::TestRetryRequestPB req;
  req.set_dest_uuid(peer_.permanent_uuid());
  req.set_num_retries(num_retries_);

  master_test_proxy_->TestRetryAsync(req, &resp_, &rpc_, BindRpcCallback());
  return true;
}

} // namespace yb::master
