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

#include <chrono>

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/ysql_ddl_verification_task.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"

DEFINE_RUNTIME_bool(retry_if_ddl_txn_verification_pending, true,
    "Whether to retry a transaction if it fails verification.");

DEFINE_RUNTIME_int32(wait_for_ysql_ddl_verification_timeout_ms, 200,
    "Timeout in milliseconds to wait for YSQL DDL transaction verification to finish");

DEFINE_test_flag(bool, disable_ysql_ddl_txn_verification, false,
    "Simulates a condition where the background process that checks whether the YSQL transaction "
    "was a success or a failure is indefinitely delayed");

DEFINE_test_flag(int32, ysql_max_random_delay_before_ddl_verification_usecs, 0,
                  "Maximum #usecs to randomly sleep before verifying a YSQL DDL transaction");

DEFINE_test_flag(bool, pause_ddl_rollback, false, "Pause DDL rollback");

DEFINE_test_flag(bool, hang_on_ddl_verification_progress, false,
    "Used in tests to simulate a hang while checking ddl verification progress.");

DEFINE_test_flag(double, ysql_fail_probability_of_catalog_writes_by_ddl_verification, 0.0,
    "Inject random failure in sys catalog writes made by ddl transaction verification");

DEFINE_test_flag(double, ysql_ddl_rollback_failure_probability, 0.0,
    "Inject random failure of ddl rollback operations");

DEFINE_test_flag(double, ysql_ddl_verification_failure_probability, 0.0,
    "Inject random failure of ddl verification operations");

using namespace std::placeholders;
using std::shared_ptr;
using std::string;
using std::vector;

namespace yb {
namespace master {

/*
 * This file contains all the logic required for YSQL DDL transaction verification. This is done
 * by maintaining the verification state for every YSQL DDL transaction in
 * 'ysql_ddl_txn_verfication_state_map_'. Each transaction id is associated with a vector of tables
 * that it affects and has 2 states.
 * 1. txn_state: This indicates whether the transaction has finished, and whether it has
 *  committed or aborted. This is updated either when the poller in ysql_transaction_ddl finishes
 * or when PG sends a ReportYsqlDdlTxnStatus RPC.
 * 2. state: This indicates the state of the transaction verification. It can be one of the
 * following:
 *   a. kDdlInProgress: This indicates that the transaction is not finished yet.
 *   b. kDdlPostProcessing: This indicates that the transaction is finished and 'txn_state'
 *      can tell us whether the transaction is committed or aborted.
 *   c. kDdlPostProcessingFailed: This indicates due to any error (such as failure to schedule
 *      callbacks/could not get reply from the transaction coordinator) we could not perform the
 *      rollback/rollforward necessary. In this case, the tables remain with the DDL state on them.
 *      If a future DDL transaction tries to modify the same table, it will re-trigger the DDL
 *      verification if it sees that the transaction is in kDdlPostProcessingFailed. A master
 *      restart will also re-trigger the DDL verification. In future we could have a background
 *      thread that periodically checks for such transactions and re-triggers the DDL verification.
 * If DDL transaction is verified successfully, it will be removed from the map. Note that DDL
 * transaction verificaion can thus be kicked off in 4 ways:
 * a) When a DDL is started, we kick off a poller through ysql_transaction_ddl that checks the
 *    transaction status and starts the DDL transaction verification once the transaction finishes.
 * b) When a DDL is finished, YSQL will send a ReportYsqlDdlTxnStatus RPC to the master with the
 *    status of the transaction. This will also trigger the DDL transaction verification.
 * c) YSQL sends IsYsqlDdlVerificationDone RPC to the master to check whether the DDL transaction
 *    verification is complete before returning to the client. This may also trigger DDL
 *    verification if the transaction is in kDdlPostProcessingFailed state.
 * d) When another DDL tries to modify the same table, it will trigger DDL verification if the
 *    old transaction is in kDdlPostProcessingFailed state.
*/
Status CatalogManager::ScheduleYsqlTxnVerification(
    const TableInfoPtr& table, const TransactionMetadata& txn,
    const LeaderEpoch& epoch) {

  bool new_transaction = CreateOrUpdateDdlTxnVerificationState(table, txn);

  if (FLAGS_TEST_disable_ysql_ddl_txn_verification) {
    LOG(INFO) << "Skip scheduling table " << table->ToString() << " for transaction verification "
              << "as TEST_disable_ysql_ddl_txn_verification is set";
    return Status::OK();
  }

  if (new_transaction) {
    return ScheduleVerifyTransaction(table, txn, epoch);
  }
  return Status::OK();
}

bool CatalogManager::CreateOrUpdateDdlTxnVerificationState(
    const TableInfoPtr& table, const TransactionMetadata& txn) {
  LockGuard lock(ddl_txn_verifier_mutex_);
  auto state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn.transaction_id);
  if (state) {
    // This transaction is already being verified. Add this table to the list of tables modified
    // by this transaction and return.
    LOG_IF(DFATAL, state->txn_state == TxnState::kCommitted)
        << "Transaction " << txn << " is already complete, but received request to verify table "
        << table;
    LOG(INFO) << "Enqueuing table " << table->ToString()
              << " to the list of tables being verified for transaction " << txn;
    state->tables.push_back(table);
    return false;
  }

  LOG(INFO) << "Enqueuing table " << table->ToString()
            << " for schema comparison for transaction " << txn;
  ysql_ddl_txn_verfication_state_map_.emplace(txn.transaction_id,
      YsqlDdlTransactionState{TxnState::kUnknown,
                              YsqlDdlVerificationState::kDdlInProgress,
                              {table}});
  return true;
}

Status CatalogManager::ScheduleVerifyTransaction(
    const TableInfoPtr& table, const TransactionMetadata& txn,
    const LeaderEpoch& epoch) {
  auto l = table->LockForRead();
  LOG(INFO) << "Enqueuing table for DDL transaction Verification: " << table->name()
            << " id: " << table->id() << " schema version: " << l->pb.version()
            << " for transaction " << txn;
  const string txn_id_pb = l->pb_transaction_id();
  auto when_done = [this, table, txn_id_pb, epoch](Result<bool> is_committed) {
    WARN_NOT_OK(YsqlTableSchemaChecker(table, txn_id_pb, is_committed, epoch),
                "YsqlTableSchemaChecker failed");
  };
  TableSchemaVerificationTask::CreateAndStartTask(
      *this, table, txn, std::move(when_done), sys_catalog_.get(), master_->client_future(),
      *master_->messenger(), epoch, true /* ddl_atomicity_enabled */);
  return Status::OK();
}

Status CatalogManager::YsqlTableSchemaChecker(TableInfoPtr table,
                                              const string& pb_txn_id,
                                              Result<bool> is_committed,
                                              const LeaderEpoch& epoch) {
  if (!is_committed.ok()) {
    auto txn = VERIFY_RESULT(FullyDecodeTransactionId(pb_txn_id));
    LockGuard lock(ddl_txn_verifier_mutex_);
    auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn);
    if (!verifier_state) {
      VLOG(3) << "Transaction " << txn << " is already verified, ignoring";
      return Status::OK();
    }

    if (verifier_state->state == YsqlDdlVerificationState::kDdlPostProcessing) {
      // Verification is already in progress.
      VLOG(3) << "Transaction " << txn << " is already being verified, ignoring";
      return Status::OK();
    }
    verifier_state->state = YsqlDdlVerificationState::kDdlPostProcessingFailed;
    return STATUS_FORMAT(IllegalState,
        "Find Transaction Status for table $0 txn: $1 failed with $2",
        table->ToString(), txn, is_committed.status());
  }

  return YsqlDdlTxnCompleteCallback(pb_txn_id, is_committed.get(), epoch);
}

Status CatalogManager::YsqlDdlTxnCompleteCallback(const string& pb_txn_id,
                                                  bool is_committed,
                                                  const LeaderEpoch& epoch) {
  SCHECK(!pb_txn_id.empty(), IllegalState,
      "YsqlDdlTxnCompleteCallback called without transaction id");
  SleepFor(MonoDelta::FromMicroseconds(RandomUniformInt<int>(0,
    FLAGS_TEST_ysql_max_random_delay_before_ddl_verification_usecs)));

  auto txn = VERIFY_RESULT(FullyDecodeTransactionId(pb_txn_id));
  LOG(INFO) << "YsqlDdlTxnCompleteCallback for transaction "
            << txn << " is_committed: " << (is_committed ? "true" : "false");

  vector<TableInfoPtr> tables;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn);
    if (!verifier_state) {
      VLOG(3) << "Transaction " << txn << " is already verified, ignoring";
      return Status::OK();
    }

    auto state = verifier_state->state;
    if (state == YsqlDdlVerificationState::kDdlPostProcessing) {
      // Verification is already in progress.
      VLOG(3) << "Transaction " << txn << " is already being verified, ignoring";
      return Status::OK();
    }

    tables = verifier_state->tables;
    verifier_state->txn_state =
        (is_committed) ? TxnState::kCommitted : TxnState::kAborted;
    verifier_state->state = YsqlDdlVerificationState::kDdlPostProcessing;
  }

  bool ddl_verification_success = true;
  for (auto& table : tables) {
    // If the table is already involved in a new DDL transaction, then txn
    // has already completed. The table will be taken care of by the new
    // transaction.
    auto table_txn_id = table->LockForRead()->pb_transaction_id();
    if (table_txn_id != pb_txn_id) {
      LOG(INFO) << "table " << table->id() << " has a new txn id " << table_txn_id
                << " and is no longer bound by txn " << pb_txn_id;
      continue;
    }
    if (table->is_index()) {
      // This is an index. If the indexed table is being deleted or marked for deletion, then skip
      // doing anything as the deletion of the table will delete this index.
      const auto& indexed_table_id = table->indexed_table_id();
      auto indexed_table = VERIFY_RESULT(FindTableById(indexed_table_id));
      if (table->IsBeingDroppedDueToDdlTxn(pb_txn_id, is_committed) &&
          indexed_table->IsBeingDroppedDueToDdlTxn(pb_txn_id, is_committed)) {
        LOG(INFO) << "Skipping DDL transaction verification for index " << table->ToString()
                << " as the indexed table " << indexed_table->ToString()
                << " is also being dropped";
        continue;
      }
    }

    if (RandomActWithProbability(FLAGS_TEST_ysql_ddl_verification_failure_probability)) {
      LOG(ERROR) << "Injected random failure for testing";
      ddl_verification_success = false;
      continue;
    }

    auto s = background_tasks_thread_pool_->SubmitFunc([this, table, txn, is_committed, epoch]() {
      auto s = YsqlDdlTxnCompleteCallbackInternal(table.get(), txn, is_committed, epoch);
      if (!s.ok()) {
        LOG(WARNING) << "YsqlDdlTxnCompleteCallback failed for table " << table->ToString()
                     << " txn " << txn << ": " << s.ToString();
        UpdateDdlVerificationState(txn, YsqlDdlVerificationState::kDdlPostProcessingFailed);
      }
    });
    if (!s.ok()) {
      ddl_verification_success = false;
    }
  }
  if (!ddl_verification_success) {
    UpdateDdlVerificationState(txn, YsqlDdlVerificationState::kDdlPostProcessingFailed);
  }
  return Status::OK();
}

Status CatalogManager::ReportYsqlDdlTxnStatus(
    const ReportYsqlDdlTxnStatusRequestPB* req, ReportYsqlDdlTxnStatusResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  DCHECK(req);
  const auto& req_txn = req->transaction_id();
  SCHECK(!req_txn.empty(), IllegalState,
      "Received ReportYsqlDdlTxnStatus request without transaction id");
  return YsqlDdlTxnCompleteCallback(req_txn, req->is_committed(), epoch);
}

struct YsqlTableDdlTxnState {
  TableInfo* table;
  TableInfo::WriteLock& write_lock;
  LeaderEpoch epoch;
  TransactionId ddl_txn_id;
};

Status CatalogManager::YsqlDdlTxnCompleteCallbackInternal(
    TableInfo* table, const TransactionId& txn_id, bool success, const LeaderEpoch& epoch) {

  TEST_PAUSE_IF_FLAG(TEST_pause_ddl_rollback);

  const auto id = "table id: " + table->id();

  auto l = table->LockForWrite();
  if (!VERIFY_RESULT(l->is_being_modified_by_ddl_transaction(txn_id))) {
    // Transaction verification completed for this table.
    VLOG(3) << "Verification of transaction " << txn_id << " for " << id
            << " is already complete, ignoring";
    return Status::OK();
  }
  LOG(INFO) << "YsqlDdlTxnCompleteCallback for " << id
            << " for transaction " << txn_id
            << ": Success: " << (success ? "true" : "false")
            << " ysql_ddl_txn_verifier_state: "
            << l->ysql_ddl_txn_verifier_state().DebugString();

  auto& metadata = l.mutable_data()->pb;

  SCHECK(l->is_running(), Aborted,
         "Unexpected table state ($0), abandoning DDL rollback for $1",
         SysTablesEntryPB_State_Name(metadata.state()), table->ToString());

  auto txn_data = YsqlTableDdlTxnState {
    .table = table,
    .write_lock = l,
    .epoch = epoch,
    .ddl_txn_id = txn_id
  };

  if (success) {
    RETURN_NOT_OK(HandleSuccessfulYsqlDdlTxn(txn_data));
  } else {
    RETURN_NOT_OK(HandleAbortedYsqlDdlTxn(txn_data));
  }
  return Status::OK();
}

Status CatalogManager::HandleSuccessfulYsqlDdlTxn(
    const YsqlTableDdlTxnState txn_data) {
  // The only DDL operations that roll-forward (i.e. take complete effect after commit) are DROP
  // TABLE and DROP COLUMN.
  auto& l = txn_data.write_lock;
  if (l->is_being_deleted_by_ysql_ddl_txn()) {
    return YsqlDdlTxnDropTableHelper(txn_data, true /* success */);
  }

  vector<string> cols_being_dropped;
  auto& mutable_pb = l.mutable_data()->pb;
  for (const auto& col : mutable_pb.schema().columns()) {
    if (col.marked_for_deletion()) {
        cols_being_dropped.push_back(col.name());
    }
  }
  if (cols_being_dropped.empty()) {
    return ClearYsqlDdlTxnState(txn_data);
  }
  Schema current_schema;
  RETURN_NOT_OK(SchemaFromPB(mutable_pb.schema(), &current_schema));
  SchemaBuilder builder(current_schema);
  std::vector<DdlLogEntry> ddl_log_entries;

  for (const auto& col : cols_being_dropped) {
    RETURN_NOT_OK(builder.RemoveColumn(col));
    ddl_log_entries.emplace_back(
          master_->clock()->Now(),
          txn_data.table->id(),
          mutable_pb,
          Format("Drop column $0", col));
  }
  SchemaToPB(builder.Build(), mutable_pb.mutable_schema());
  return YsqlDdlTxnAlterTableHelper(
      txn_data, ddl_log_entries, "" /* new_table_name */, true /* success */);
}

Status CatalogManager::HandleAbortedYsqlDdlTxn(const YsqlTableDdlTxnState txn_data) {
  auto& mutable_pb = txn_data.write_lock.mutable_data()->pb;
  const auto& ddl_state = mutable_pb.ysql_ddl_txn_verifier_state(0);
  if (ddl_state.contains_create_table_op()) {
    // This table was created in this aborted transaction. Drop the xCluster streams and the table.
    RETURN_NOT_OK(DropXClusterStreamsOfTables({txn_data.table->id()}));

    return YsqlDdlTxnDropTableHelper(txn_data, false /* success */);
  }
  if (ddl_state.contains_alter_table_op()) {
    std::vector<DdlLogEntry> ddl_log_entries;
    ddl_log_entries.emplace_back(
        master_->clock()->Now(),
        txn_data.table->id(),
        mutable_pb,
        "Rollback of DDL Transaction");
    mutable_pb.mutable_schema()->CopyFrom(ddl_state.previous_schema());
    const string new_table_name = ddl_state.previous_table_name();
    mutable_pb.set_name(new_table_name);
    return YsqlDdlTxnAlterTableHelper(
        txn_data, ddl_log_entries, new_table_name, false /* success */);
  }

  // This must be a failed Delete transaction.
  DCHECK(ddl_state.contains_drop_table_op());
  return ClearYsqlDdlTxnState(txn_data);
}

Status CatalogManager::ClearYsqlDdlTxnState(const YsqlTableDdlTxnState txn_data) {
  auto& pb = txn_data.write_lock.mutable_data()->pb;
  VLOG(3) << "Clearing ysql_ddl_txn_verifier_state from table "
          << txn_data.table->id() << ", txn_id: " << txn_data.ddl_txn_id;
  pb.clear_ysql_ddl_txn_verifier_state();
  pb.clear_transaction();

  RETURN_NOT_OK(sys_catalog_->Upsert(txn_data.epoch, txn_data.table));
  if (RandomActWithProbability(
      FLAGS_TEST_ysql_fail_probability_of_catalog_writes_by_ddl_verification)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }
  txn_data.write_lock.Commit();
  RemoveDdlTransactionState(txn_data.table->id(), {txn_data.ddl_txn_id});
  return Status::OK();
}

Status CatalogManager::YsqlDdlTxnAlterTableHelper(const YsqlTableDdlTxnState txn_data,
                                                  const std::vector<DdlLogEntry>& ddl_log_entries,
                                                  const string& new_table_name,
                                                  bool success) {
  auto& table_pb = txn_data.write_lock.mutable_data()->pb;
  const int target_schema_version = table_pb.version() + 1;
  table_pb.set_version(target_schema_version);
  table_pb.set_updates_only_index_permissions(false);
  table_pb.set_state(SysTablesEntryPB::ALTERING);
  table_pb.set_state_msg(
    strings::Substitute("Alter table version=$0 ts=$1", table_pb.version(), LocalTimeAsString()));

  VLOG(3) << "Clearing ysql_ddl_txn_verifier_state from table "
          << txn_data.table->id() << ", txn_id: " << txn_data.ddl_txn_id;
  table_pb.clear_ysql_ddl_txn_verifier_state();
  table_pb.clear_transaction();

  // Update sys-catalog with the new table schema.
  RETURN_NOT_OK(UpdateSysCatalogWithNewSchema(
        txn_data.table,
        ddl_log_entries,
        "" /* new_namespace_id */,
        new_table_name,
        txn_data.epoch,
        nullptr /* resp */));

  if (RandomActWithProbability(
      FLAGS_TEST_ysql_fail_probability_of_catalog_writes_by_ddl_verification)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }

  txn_data.write_lock.Commit();

  // Enqueue this transaction to be notified when the alter operation is updated.
  auto table = txn_data.table;
  table->AddDdlTxnWaitingForSchemaVersion(target_schema_version, txn_data.ddl_txn_id);

  auto action = success ? "roll forward" : "rollback";
  LOG(INFO) << "Sending Alter Table request as part of " << action
            << " for table " << table->name();
  if (RandomActWithProbability(FLAGS_TEST_ysql_ddl_rollback_failure_probability)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }
  return SendAlterTableRequestInternal(table, TransactionId::Nil(), txn_data.epoch);
}

Status CatalogManager::YsqlDdlTxnDropTableHelper(
    const YsqlTableDdlTxnState txn_data, bool success) {
  auto table = txn_data.table;
  txn_data.write_lock.Commit();
  DeleteTableRequestPB dtreq;
  DeleteTableResponsePB dtresp;

  dtreq.mutable_table()->set_table_name(table->name());
  dtreq.mutable_table()->set_table_id(table->id());
  dtreq.set_is_index_table(table->is_index());
  auto action = success ? "roll forward" : "rollback";
  LOG(INFO) << "Delete table " << table->id() << " as part of " << action;

  if (RandomActWithProbability(FLAGS_TEST_ysql_ddl_rollback_failure_probability)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }
  return DeleteTableInternal(&dtreq, &dtresp, nullptr /* rpc */, txn_data.epoch);
}

Status CatalogManager::IsYsqlDdlVerificationDone(
    const IsYsqlDdlVerificationDoneRequestPB* req, IsYsqlDdlVerificationDoneResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {

  if (GetAtomicFlag(&FLAGS_TEST_hang_on_ddl_verification_progress)) {
    TEST_SYNC_POINT("YsqlDdlHandler::IsYsqlDdlVerificationDone:Fail");
  }

  auto txn = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
  const auto& req_txn = txn.transaction_id;
  SCHECK(!FLAGS_TEST_pause_ddl_rollback, InvalidArgument,
      "DDL Rollback is paused for txn $0", txn.transaction_id);

  bool is_done = false;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    is_done = !ysql_ddl_txn_verfication_state_map_.contains(req_txn);
  }
  resp->set_done(is_done);
  VLOG(1) << "Received IsYsqlDdlVerificationDone request for transaction " << req_txn
          << " responding with " << (is_done ? "true" : "false");
  WARN_NOT_OK(TriggerDdlVerificationIfNeeded(txn, epoch),
              Format("Failed to re-trigger DDL verification for transaction $0", req_txn));
  return Status::OK();
}

void CatalogManager::UpdateDdlVerificationState(const TransactionId& txn,
                                                YsqlDdlVerificationState state) {
  LockGuard lock(ddl_txn_verifier_mutex_);
  auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn);
  if (verifier_state) {
    LOG(INFO) << "Updating the verification state for " << txn << " to " << state;
    verifier_state->state = state;
  }
}

void CatalogManager::RemoveDdlTransactionStateUnlocked(
    const TableId& table_id, const std::vector<TransactionId>& txn_ids) {
  if (txn_ids.size() == 0) {
    return;
  }
  for (const auto& txn_id : txn_ids) {
    auto iter = ysql_ddl_txn_verfication_state_map_.find(txn_id);
    if (iter == ysql_ddl_txn_verfication_state_map_.end()) {
      continue;
    }
    LOG(INFO) << "Removing " << table_id << " from DDL Verification state for " << txn_id;
    auto& tables = iter->second.tables;
    const auto num_tables = std::erase_if(tables,
        [&table_id](const TableInfoPtr& table) {
          return table->id() == table_id;
    });
    DCHECK_LE(num_tables, 1);
    if (tables.empty()) {
      LOG(INFO) << "Erasing DDL Verification state for " << txn_id;
      ysql_ddl_txn_verfication_state_map_.erase(iter);
    } else {
      VLOG(1) << "DDL Verification state for " << txn_id << " has "
              << tables.size() << " tables remaining";
    }
  }
}

void CatalogManager::RemoveDdlTransactionState(
    const TableId& table_id, const std::vector<TransactionId>& txn_ids) {
  LockGuard lock(ddl_txn_verifier_mutex_);
  RemoveDdlTransactionStateUnlocked(table_id, txn_ids);
}

Status CatalogManager::TriggerDdlVerificationIfNeeded(
    const TransactionMetadata& txn, const LeaderEpoch& epoch) {
  if (FLAGS_TEST_disable_ysql_ddl_txn_verification) {
    LOG(INFO) << "Skip transaction verification as TEST_disable_ysql_ddl_txn_verification is set";
    return Status::OK();
  }

  TableInfoPtr table;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn.transaction_id);
    if (!verifier_state) {
      VLOG(3) << "Not triggering Ddl Verification as transaction already completed " << txn;
      return Status::OK();
    }

    auto state = verifier_state->state;
    if (state != YsqlDdlVerificationState::kDdlPostProcessingFailed) {
      VLOG(3) << "Not triggering Ddl Verification as it is in progress " << txn
              << ", state: " << state;
      return Status::OK();
    }

    if (verifier_state->txn_state != TxnState::kUnknown) {
      // We already know whether this transaction is a success or a failure. We don't need to poll
      // the transaction coordinator at this point. We can simply invoke post DDL verification
      // directly.
      const bool is_committed = verifier_state->txn_state == TxnState::kCommitted;
      string pb_txn_id;
      vector<TableId> table_ids;
      for (const auto& table : verifier_state->tables) {
        table_ids.push_back(table->id());
        pb_txn_id = table->LockForRead()->pb_transaction_id();
        if (pb_txn_id.empty()) {
          // The table involved in ddl transaction txn_id has already finalized
          // with a new schema version, but verifier_state for txn_id isn't
          // cleared which implies the new schema version has not reached
          // all of its tablets yet. Call SendAlterTableRequestInternal to
          // sync them up. If fails, reschedule TriggerDdlVerificationIfNeeded
          // with a delay.
          auto s = SendAlterTableRequestInternal(
                   table, TransactionId::Nil(), epoch);
          if (!s.ok()) {
            LOG(WARNING) << "SendAlterTableRequestInternal failed, table: " << table->id();
            ScheduleTriggerDdlVerificationIfNeeded(txn, epoch, 500 /* delay_ms */);
          }
          continue;
        }
        auto txn_id = CHECK_RESULT(FullyDecodeTransactionId(pb_txn_id));
        if (txn_id != txn.transaction_id) {
          // This can happen when the table schema has already finalized, but
          // the table's verifier state hasn't been cleared from DDL
          // transaction txn.transaction_id yet which means it is still waiting
          // for the finalized schema version to reach all of its tablets.
          // However the table is now involved with a new DDL transaction
          // txn_id, which represents a new DDL transaction that happens after
          // the already finalized DDL transaction txn.transaction_id.
          // In this case we clear the table from txn.transaction_id.
          // The new DDL transaction txn_id will take care of syncing up the
          // table's schema version with its tablets.
          LOG(WARNING) << "pb_txn_id " << txn_id << " on table "
                       << table->id() << " differs from txn.transaction_id "
                       << txn.transaction_id;
          RemoveDdlTransactionStateUnlocked(table->id(), {txn.transaction_id});
          continue;
        }
        return background_tasks_thread_pool_->SubmitFunc(
          [this, table, pb_txn_id, is_committed, epoch]() {
              WARN_NOT_OK(YsqlDdlTxnCompleteCallback(pb_txn_id, is_committed, epoch),
                          Format("YsqlDdlTxnCompleteCallback failed, table: $0",
                                 table->id()));
          }
        );
      }
      VLOG(3) << "All tables " << VectorToString(table_ids)
              << " in transaction " << txn << " have pb_txn_id cleared"
              << " or have a new txn_id";
      return Status::OK();
    }
    table = verifier_state->tables.front();
  }

  // Schedule transaction verification.
  auto l = table->LockForRead();
  LOG(INFO) << "Enqueuing table for DDL transaction Verification: " << table->name()
            << " id: " << table->id() << " schema version: " << l->pb.version()
            << " for transaction " << txn;

  const string txn_id_pb = l->pb_transaction_id();
  auto when_done = [this, table, txn_id_pb, epoch](Result<bool> is_committed) {
    WARN_NOT_OK(YsqlTableSchemaChecker(table, txn_id_pb, is_committed, epoch),
                "YsqlTableSchemaChecker failed");
  };
  TableSchemaVerificationTask::CreateAndStartTask(
      *this, table, txn, std::move(when_done), sys_catalog_.get(), master_->client_future(),
      *master_->messenger(), epoch, true /* ddl_atomicity_enabled */);
  return Status::OK();
}

// Call TriggerDdlVerificationIfNeeded with a delay.
void CatalogManager::ScheduleTriggerDdlVerificationIfNeeded(
    const TransactionMetadata& txn, const LeaderEpoch& epoch, int32_t delay_ms) {
  Scheduler().Schedule([this, txn, epoch, delay_ms](const Status& status) {
    WARN_NOT_OK(background_tasks_thread_pool_->SubmitFunc([this, txn, epoch, delay_ms]() {
      auto s = TriggerDdlVerificationIfNeeded(txn, epoch);
      if (!s.ok()) {
        LOG(WARNING) << "Failed to re-trigger DDL verification for transaction " << txn;
        ScheduleTriggerDdlVerificationIfNeeded(txn, epoch, delay_ms);
      }
    }),
    Format("Failed to schedule DDL verification for transaction $0", txn));
  }, std::chrono::milliseconds(delay_ms));
}

} // namespace master
} // namespace yb
