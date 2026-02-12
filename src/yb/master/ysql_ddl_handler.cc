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

#include <chrono>

#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master.h"
#include "yb/master/object_lock_info_manager.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/ysql_ddl_verification_task.h"

#include "yb/rpc/scheduler.h"

#include "yb/util/sync_point.h"

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

DEFINE_test_flag(bool, disable_release_object_locks_on_ddl_verification, false,
    "When set, skip release object lock rpcs to tservers triggered at the end of DDL verification, "
    "that release object locks acquired by the DDL.");

DECLARE_bool(ysql_yb_enable_ddl_savepoint_support);

using namespace std::placeholders;
using std::shared_ptr;
using std::string;
using std::vector;

namespace yb::master {

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
              << " to the list of tables being verified for transaction " << txn
              << ", txn_state: " << state->txn_state << ", state: " << state->state;
    state->tables.push_back(table);
    return false;
  }

  LOG(INFO) << "Enqueuing table " << table->ToString()
            << " for schema comparison for transaction " << txn;
  ysql_ddl_txn_verfication_state_map_.emplace(txn.transaction_id,
      YsqlDdlTransactionState{TxnState::kUnknown,
                              YsqlDdlVerificationState::kDdlInProgress,
                              {table}, {} /* processed_tables */, {} /* nochange_tables */});
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
  auto when_done = [this, table, txn_id_pb, epoch](Result<std::optional<bool>> is_committed) {
    WARN_NOT_OK(YsqlTableSchemaChecker(table, txn_id_pb, is_committed, epoch),
                "YsqlTableSchemaChecker failed");
  };
  TableSchemaVerificationTask::CreateAndStartTask(
      *this, table, txn, std::move(when_done), sys_catalog_, master_->client_future(),
      *master_->messenger(), epoch, true /* ddl_atomicity_enabled */);
  return Status::OK();
}

Status CatalogManager::YsqlTableSchemaChecker(TableInfoPtr table,
                                              const string& pb_txn_id,
                                              Result<std::optional<bool>> is_committed,
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

  return YsqlDdlTxnCompleteCallback(table, pb_txn_id, is_committed.get(), epoch, __FUNCTION__);
}

bool CatalogManager::HasDdlVerificationState(const TransactionId& txn) const {
  LockGuard lock(ddl_txn_verifier_mutex_);
  return ysql_ddl_txn_verfication_state_map_.contains(txn);
}

Status CatalogManager::YsqlDdlTxnCompleteCallback(TableInfoPtr table,
                                                  const string& pb_txn_id,
                                                  std::optional<bool> is_committed,
                                                  const LeaderEpoch& epoch,
                                                  const std::string& debug_caller_info) {
  SCHECK(!pb_txn_id.empty(), IllegalState,
      Format("YsqlDdlTxnCompleteCallback called without transaction id: $0", debug_caller_info));
  SleepFor(MonoDelta::FromMicroseconds(RandomUniformInt<int>(0,
    FLAGS_TEST_ysql_max_random_delay_before_ddl_verification_usecs)));

  auto txn = VERIFY_RESULT(FullyDecodeTransactionId(pb_txn_id));
  LOG(INFO) << "YsqlDdlTxnCompleteCallback for transaction "
            << txn << " is_committed: "
            << (is_committed.has_value() ? (*is_committed ? "true" : "false") : "nullopt")
            << ", debug_caller_info " << debug_caller_info;

  vector<TableInfoPtr> tables;
  std::unordered_set<TableId> processed_tables;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn);
    if (!verifier_state) {
      VLOG(3) << "Transaction " << txn << " is already verified, ignoring";
      return Status::OK();
    }

    auto state = verifier_state->state;
    auto txn_state = is_committed.has_value() ?
        (*is_committed ? TxnState::kCommitted : TxnState::kAborted) : TxnState::kNoChange;
    if (state == YsqlDdlVerificationState::kDdlPostProcessing) {
      // We used to return Status::OK() here on the grounds that the txn is
      // already being verified and we assumed verifier_state->tables represent
      // all of the tables involved in txn and they are taken care of by
      // calling YsqlDdlTxnCompleteCallbackInternal on each of them below.
      // However, verifier_state->tables may not include all of the tables
      // involved in txn. It is possible that a table is only added into the
      // txn after the txn is already in kDdlPostProcessing state. For example,
      // a txn involves three tables t1, t2, t3. After t1 and t2 are added to txn,
      // the txn is aborted due to some reason (e.g., conflict). In this case
      // YsqlDdlTxnCompleteCallbackInternal is only called on t1 and t2 and
      // the state is set to kDdlPostProcessing before t3 gets added. Later when
      // t3 gets added, if we return Status::OK() here, then t3 will never be
      // processed. Therefore we need to call YsqlDdlTxnCompleteCallbackInternal
      // to process t3. It is fine to reprocess t1 and t2, that will result in a
      // no-op, except for delete table operation for which we'll detect and avoid
      // reprocessing.
      // Some alter table DDL statements only increment table schema version and does not make
      // any table schema change, this is represented by TxnState::kNoChange. In this case
      // it does not matter whether the PG DDL transaction is committed or aborted.
      if (txn_state != verifier_state->txn_state &&
          txn_state != TxnState::kNoChange && verifier_state->txn_state != TxnState::kNoChange) {
        return STATUS_FORMAT(IllegalState, "Mismatch in txn_state for transaction $0", txn);
      }
    } else {
      if (verifier_state->txn_state != TxnState::kCommitted &&
          verifier_state->txn_state != TxnState::kAborted) {
        verifier_state->txn_state = txn_state;
      }
      verifier_state->state = YsqlDdlVerificationState::kDdlPostProcessing;
    }
    tables = verifier_state->tables;
    processed_tables = verifier_state->processed_tables;
    if (txn_state == TxnState::kNoChange) {
      DCHECK(table);
      verifier_state->nochange_tables.insert(table->id());
    }

    // If we have used a table whose schema does not change for schema comparison, then we
    // cannot decide whether the transaction is committed or aborted. All we can tell is that
    // the transaction is terminated. In this case, if there are more tables in this
    // transaction, we should try to use another table to do the schema comparison.
    // For example, a DDL like "alter table mytable add constraint x_unique unique(x)"
    // does not change the table mytable's DocDB schema, but it creates a new index
    // x_unique in DocDB. We cannot use mytable to decide whether the transaction has
    // committed or aborted, but we can use x_unique for that purpose.
    if (!is_committed.has_value() &&
      // Try to find a table that may have its schema changed before/after the DDL.
        std::find_if(
          tables.cbegin(), tables.cend(), [verifier_state](const TableInfoPtr& table) {
            return !verifier_state->nochange_tables.contains(table->id());
          }) != tables.cend()) {
      // Set to kDdlPostProcessingFailed so we can restart the verification task next time,
      // where we will try to choose a different table.
      UpdateDdlVerificationStateUnlocked(txn, YsqlDdlVerificationState::kDdlPostProcessingFailed);
      return Status::OK();
    }
  }

  bool ddl_verification_success = true;
  for (auto& table : tables) {
    if (processed_tables.contains(table->id())) {
      VLOG(1) << "DDL already processed on table " << table->id();
      continue;
    }
    auto table_txn_id = table->LockForRead()->pb_transaction_id();
    // If the table is no longer involved in a DDL transaction, then txn has already completed.
    if (table_txn_id.empty()) {
      LOG(INFO) << "table " << table->id() << " has no txn id"
                << " so is no longer bound by txn " << txn;
      RemoveDdlTransactionState(table->id(), {txn});
      continue;
    }
    // If the table is already involved in a new DDL transaction, then txn
    // has already completed. The table will be taken care of by the new
    // transaction.
    if (table_txn_id != pb_txn_id) {
      auto new_txn = VERIFY_RESULT(FullyDecodeTransactionId(table_txn_id));
      LOG(INFO) << "table " << table->id() << " has a new txn id " << new_txn
                << " and is no longer bound by txn " << txn;
      RemoveDdlTransactionState(table->id(), {txn});
      continue;
    }
    if (table->is_index() && is_committed.has_value()) {
      // This is an index. If the indexed table is being deleted or marked for deletion, then skip
      // doing anything as the deletion of the table will delete this index.
      const auto& indexed_table_id = table->indexed_table_id();
      auto indexed_table = VERIFY_RESULT(FindTableById(indexed_table_id));
      if (table->IsBeingDroppedDueToDdlTxn(pb_txn_id, *is_committed) &&
          indexed_table->IsBeingDroppedDueToDdlTxn(pb_txn_id, *is_committed)) {
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
  // When req->is_committed() is known (i.e., not kNoChange), the first argument table is not
  // needed.
  return YsqlDdlTxnCompleteCallback(nullptr /* table */, req_txn, req->is_committed(),
                                    epoch, __FUNCTION__);
}

struct YsqlTableDdlTxnState {
  TableInfo* table;
  TableInfo::WriteLock& write_lock;
  LeaderEpoch epoch;
  TransactionId ddl_txn_id;
};

Status CatalogManager::YsqlDdlTxnCompleteCallbackInternal(
    TableInfo* table, const TransactionId& txn_id,
    std::optional<bool> success, const LeaderEpoch& epoch) {

  TEST_PAUSE_IF_FLAG(TEST_pause_ddl_rollback);

  const auto id = "table id: " + table->id();

  auto l = table->LockForWrite();
  if (!VERIFY_RESULT(l->is_being_modified_by_ddl_transaction(txn_id))) {
    // Transaction verification completed for this table.
    VLOG(3) << "Verification of transaction " << txn_id << " for " << id
            << " is already complete, ignoring";
    return Status::OK();
  }
  LOG_WITH_FUNC(INFO) << id << " for transaction " << txn_id
                      << ": Success: "
                      << (success.has_value() ? (*success ? "true" : "false") : "nullopt")
                      << " ysql_ddl_txn_verifier_state: "
                      << AsString(l->ysql_ddl_txn_verifier_state());

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

  if (success.has_value()) {
    if (*success) {
      RETURN_NOT_OK(HandleSuccessfulYsqlDdlTxn(txn_data));
    } else {
      RETURN_NOT_OK(HandleAbortedYsqlDdlTxn(txn_data));
    }
  } else {
    // If success is nullopt, it represents a transaction where all DDL statements are either that:
    // 1. only increments the schema version of the table without any table schema change.
    // 2. were ambiguous to determine and their commit or abort ends up with the same schema.
    //    example: BEGIN; CREATE TABLE; DROP TABLE; COMMIT or ROLLBACK;
    // We can choose either COMMIT or ABORT. It doesn't matter. So we choose COMMIT here.
    VLOG(3) << "Ysql DDL transaction " << txn_id << " for table " << table->ToString()
            << " is ambiguous, treating it as a success";
    RETURN_NOT_OK(HandleSuccessfulYsqlDdlTxn(txn_data));
  }
  return Status::OK();
}

Status CatalogManager::HandleSuccessfulYsqlDdlTxn(
    const YsqlTableDdlTxnState txn_data) {
  // The only DDL operations that roll-forward (i.e. take complete effect after commit) are DROP
  // TABLE and DROP COLUMN.
  auto& l = txn_data.write_lock;
  if (l->is_being_deleted_by_ysql_ddl_txn()) {
    return YsqlDdlTxnDropTableHelper(txn_data, /*success=*/true);
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
      txn_data, ddl_log_entries, /*new_table_name=*/"", /*success=*/true);
}

Status CatalogManager::HandleAbortedYsqlDdlTxn(const YsqlTableDdlTxnState txn_data) {
  return RollbackYsqlTxnDdlStates(
      txn_data, false /* is_rollback_to_subtxn */);
}

// Helper function to rollback DDL states for the given txn_data in the segment
// [rollback_till_ddl_state_index, ysql_ddl_txn_verifier_state().size())
Status CatalogManager::RollbackYsqlTxnDdlStates(
    const YsqlTableDdlTxnState txn_data, bool is_rollback_to_subtxn,
    int rollback_till_ddl_state_index) {
  auto& l = txn_data.write_lock;
  RSTATUS_DCHECK(
      rollback_till_ddl_state_index >= 0 &&
          rollback_till_ddl_state_index < l->ysql_ddl_txn_verifier_state().size(),
      IllegalState,
      Format(
          "Invalid start index $0 to rollback to for transaction $1",
          rollback_till_ddl_state_index, txn_data.ddl_txn_id));

  auto& mutable_pb = l.mutable_data()->pb;
  // A table can undergo DDL operations in the following order:
  // a. 0 or 1 CREATE TABLE
  // b. 0 or more ALTER TABLE
  // c. 0 or 1 DROP TABLE
  const bool table_created_in_txn_segment =
      mutable_pb.ysql_ddl_txn_verifier_state(rollback_till_ddl_state_index)
          .contains_create_table_op();
  const YsqlDdlTxnVerifierStatePB& first_ddl_state =
      mutable_pb.ysql_ddl_txn_verifier_state(rollback_till_ddl_state_index);
  if (table_created_in_txn_segment) {
    RSTATUS_DCHECK_EQ(
        rollback_till_ddl_state_index, 0, IllegalState,
        "Corrupt ysql_ddl_txn_verifier_state. A table creation could have only happened as part of "
        "the first sub-transaction of ysql_ddl_txn_verifier_state.");

    // This table was created in this aborted transaction segment. Drop the xCluster streams and the
    // table.
    RETURN_NOT_OK(YsqlDdlTxnDropTableHelper(txn_data, /*success=*/false, is_rollback_to_subtxn));

    return DropXClusterStreamsOfTables({txn_data.table->id()});
  }

  if (!first_ddl_state.contains_alter_table_op()) {
    // The first_ddl_state is neither a CREATE nor an ALTER. So it must be DELETE and a single such
    // state should be present.
    RSTATUS_DCHECK(
        first_ddl_state.contains_drop_table_op(), IllegalState,
        Format(
            "Corrupt ysql_ddl_txn_verifier_state. "
            "$0 index unexpectedly doesn't include a drop table operation.",
            rollback_till_ddl_state_index));
    DCHECK_EQ(
        mutable_pb.ysql_ddl_txn_verifier_state_size(), rollback_till_ddl_state_index + 1);
    return ClearYsqlDdlTxnState(txn_data, rollback_till_ddl_state_index);
  }

  // There might be more than one YsqlDdlTxnVerifierStatePB with alter table op but since we are
  // rolling back the transaction, we need the schema of the table at the start of the transaction
  // which is stored in the previous_schema field of the first ddl state.
  DCHECK(first_ddl_state.contains_alter_table_op());
  std::vector<DdlLogEntry> ddl_log_entries;
  ddl_log_entries.emplace_back(
      master_->clock()->Now(),
      txn_data.table->id(),
      mutable_pb,
      is_rollback_to_subtxn ? "Rollback of SubTransactions" : "Rollback of DDL Transaction");
  mutable_pb.mutable_schema()->CopyFrom(first_ddl_state.previous_schema());
  const string new_table_name = first_ddl_state.previous_table_name();
  mutable_pb.set_name(new_table_name);
  if (first_ddl_state.has_previous_next_column_id()) {
    mutable_pb.set_next_column_id(first_ddl_state.previous_next_column_id());
  }
  return YsqlDdlTxnAlterTableHelper(
      txn_data, ddl_log_entries, new_table_name, /*success=*/false,
      rollback_till_ddl_state_index);
}

void RemoveDdlTxnVerifierStateFromIndex(
    SysTablesEntryPB& table_pb, int rollback_till_ddl_state_index) {
  for (int idx = table_pb.ysql_ddl_txn_verifier_state_size() - 1;
       idx >= rollback_till_ddl_state_index; idx--) {
    table_pb.mutable_ysql_ddl_txn_verifier_state()->RemoveLast();
  }
}

Status CatalogManager::ClearYsqlDdlTxnState(
    const YsqlTableDdlTxnState txn_data, int rollback_till_ddl_state_index) {
  auto& pb = txn_data.write_lock.mutable_data()->pb;
  VLOG(3) << "Clearing ysql_ddl_txn_verifier_state from table "
          << txn_data.table->id() << ", txn_id: " << txn_data.ddl_txn_id
          << ", rollback_till_ddl_state_index: " << rollback_till_ddl_state_index;

  auto final_cleanup = rollback_till_ddl_state_index == 0;
  if (final_cleanup) {
    pb.clear_ysql_ddl_txn_verifier_state();
    pb.clear_transaction();

    RETURN_NOT_OK(
        GetXClusterManager()->ClearXClusterFieldsAfterYsqlDDL(txn_data.table, pb, txn_data.epoch));
  } else {
    // Only a part of the ysql_ddl_txn_verifier_state needs to be cleared.
    // Represents a rollback to a sub-transaction.
    RemoveDdlTxnVerifierStateFromIndex(pb, rollback_till_ddl_state_index);
  }

  RETURN_NOT_OK(sys_catalog_->Upsert(txn_data.epoch, txn_data.table));
  if (RandomActWithProbability(
      FLAGS_TEST_ysql_fail_probability_of_catalog_writes_by_ddl_verification)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }
  txn_data.write_lock.Commit();
  if (final_cleanup) {
    RemoveDdlTransactionState(txn_data.table->id(), {txn_data.ddl_txn_id});
  } else {
    RemoveDdlRollbackToSubTxnState(txn_data.table->id(), txn_data.ddl_txn_id);
  }
  return Status::OK();
}

Status CatalogManager::YsqlDdlTxnAlterTableHelper(const YsqlTableDdlTxnState txn_data,
                                                  const std::vector<DdlLogEntry>& ddl_log_entries,
                                                  const string& new_table_name,
                                                  bool success,
                                                  int rollback_till_ddl_state_index) {
  RSTATUS_DCHECK(
      rollback_till_ddl_state_index == 0 || FLAGS_ysql_yb_enable_ddl_savepoint_support,
      InternalError, "Unexpected value of rollback_till_ddl_state_index");

  auto& table_pb = txn_data.write_lock.mutable_data()->pb;
  const int target_schema_version = table_pb.version() + 1;
  table_pb.set_version(target_schema_version);
  table_pb.set_updates_only_index_permissions(false);
  table_pb.set_state(SysTablesEntryPB::ALTERING);
  table_pb.set_state_msg(
    strings::Substitute("Alter table version=$0 ts=$1", table_pb.version(), LocalTimeAsString()));

  auto final_cleanup = rollback_till_ddl_state_index == 0;
  if (final_cleanup) {
    VLOG(3) << "Clearing all ysql_ddl_txn_verifier_state from table " << txn_data.table->id()
            << ", txn_id: " << txn_data.ddl_txn_id;
    table_pb.clear_ysql_ddl_txn_verifier_state();
    table_pb.clear_transaction();
  } else {
    VLOG(3) << "Clearing ysql_ddl_txn_verifier_state from index: "
            << rollback_till_ddl_state_index << " for table: " << txn_data.table->id()
            << ", txn_id: " << txn_data.ddl_txn_id;
    RemoveDdlTxnVerifierStateFromIndex(table_pb, rollback_till_ddl_state_index);
  }

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

  auto table = txn_data.table;
  if (final_cleanup) {
    // Enqueue this transaction to be notified when the alter operation is updated.
    table->AddDdlTxnWaitingForSchemaVersion(target_schema_version, txn_data.ddl_txn_id);
  } else {
    table->AddDdlTxnForRollbackToSubTxnWaitingForSchemaVersion(
        target_schema_version, txn_data.ddl_txn_id);
  }

  auto action = success ? "roll forward"
                        : (final_cleanup ? "rollback" : "rollback to sub-transaction");
  LOG(INFO) << "Sending Alter Table request as part of " << action << " for table "
            << table->name();
  if (RandomActWithProbability(FLAGS_TEST_ysql_ddl_rollback_failure_probability)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }
  return SendAlterTableRequestInternal(table, TransactionId::Nil(), txn_data.epoch);
}

Status CatalogManager::YsqlDdlTxnDropTableHelper(
    const YsqlTableDdlTxnState txn_data, bool success, bool is_rollback_to_subtxn) {
  // TableInfo::ysql_ddl_txn_verifier_state and TableInfo::transaction are cleared once all tablets
  // are deleted (in CheckTableDeleted).
  auto table = txn_data.table;
  txn_data.write_lock.Commit();
  DeleteTableRequestPB dtreq;
  DeleteTableResponsePB dtresp;

  dtreq.mutable_table()->set_table_name(table->name());
  dtreq.mutable_table()->set_table_id(table->id());
  dtreq.set_is_index_table(table->is_index());
  auto action =
      success ? "roll forward" : (is_rollback_to_subtxn ? "rollback to sub-txn" : "rollback");
  LOG(INFO) << "Delete table " << table->id() << " as part of " << action
            << " with is_rollback_to_subtxn: " << is_rollback_to_subtxn;

  if (RandomActWithProbability(FLAGS_TEST_ysql_ddl_rollback_failure_probability)) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }
  // Mark that we have called delete table on this table.
  if (!is_rollback_to_subtxn) {
    LockGuard lock(ddl_txn_verifier_mutex_);
    auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn_data.ddl_txn_id);
    if (!verifier_state) {
      VLOG(3) << "Transaction " << txn_data.ddl_txn_id << " is already verified, ignoring";
      return Status::OK();
    }
    if (verifier_state->processed_tables.contains(table->id())) {
      VLOG(1) << "Delete table already called on table " << table->id()
              << " in txn " << txn_data.ddl_txn_id;
      return Status::OK();
    }
    verifier_state->processed_tables.insert(table->id());
  }

  RETURN_NOT_OK(DeleteTableInternal(&dtreq, &dtresp, nullptr /* rpc */, txn_data.epoch));
  if (is_rollback_to_subtxn) {
    RemoveDdlRollbackToSubTxnState(table->id(), txn_data.ddl_txn_id);
  }
  return Status::OK();
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

void CatalogManager::UpdateDdlVerificationStateUnlocked(const TransactionId& txn,
                                                        YsqlDdlVerificationState state) {
  auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn);
  if (verifier_state) {
    LOG(INFO) << "Updating the verification state for " << txn << " to " << state;
    verifier_state->state = state;
  }
}
void CatalogManager::UpdateDdlVerificationState(const TransactionId& txn,
                                                YsqlDdlVerificationState state) {
  LockGuard lock(ddl_txn_verifier_mutex_);
  UpdateDdlVerificationStateUnlocked(txn, state);
}

void CatalogManager::UpdateDdlRollbackToSubTxnStateUnlocked(
    const TransactionId& txn, const SubTransactionId sub_txn_id,
    YsqlDdlSubTransactionRollbackState state) {
  auto current_state = FindOrNull(ysql_ddl_txn_undergoing_subtransaction_rollback_map_, txn);
  if (current_state && current_state->sub_txn == sub_txn_id) {
    LOG(INFO) << "Updating the rollback to sub-transaction state for " << txn
              << ", sub_transaction_id: " << sub_txn_id << " to " << state;
    current_state->state = state;
  }
}

void CatalogManager::UpdateDdlRollbackToSubTxnState(
    const TransactionId& txn, const SubTransactionId sub_txn_id,
    YsqlDdlSubTransactionRollbackState state) {
  LockGuard lock(ddl_txn_verifier_mutex_);
  UpdateDdlRollbackToSubTxnStateUnlocked(txn, sub_txn_id, state);
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

    // If savepoint support is enabled, also delete from the
    // ysql_ddl_txn_undergoing_subtransaction_rollback_map_ map since the entire
    // txn is going away for the table.
    if (FLAGS_ysql_yb_enable_ddl_savepoint_support) {
      RemoveDdlRollbackToSubTxnStateUnlocked(table_id, txn_id);
    }

    if (tables.empty()) {
      LOG(INFO) << "Erasing DDL Verification state for " << txn_id;
      ysql_ddl_txn_verfication_state_map_.erase(iter);
      // At this point, we can be sure that the docdb schema changes have been applied.
      // For instance, consider the case of an ALTER.
      // 1. Either the alter waits inline successfully before issuing the commit,
      // 2. or when the above times out, this branch is involed by the multi step
      //    TableSchemaVerificationTask's callback post the schema changes have been applied.
      if (!FLAGS_TEST_disable_release_object_locks_on_ddl_verification) {
        WARN_NOT_OK(
            background_tasks_thread_pool_->SubmitFunc([this, txn_id]() {
              DoReleaseObjectLocksIfNecessary(txn_id);
            }),
            Format("Failed to submit task for releasing exclusive object locks of txn $0", txn_id));
      }
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

void CatalogManager::RemoveDdlRollbackToSubTxnStateUnlocked(
    const TableId& table_id, TransactionId txn_id) {
  auto iter = ysql_ddl_txn_undergoing_subtransaction_rollback_map_.find(txn_id);
  if (iter == ysql_ddl_txn_undergoing_subtransaction_rollback_map_.end()) {
    return;
  }

  LOG(INFO) << "Removing " << table_id << " from DDL rollback to sub-transaction state for "
            << txn_id;
  auto& tables = iter->second.tables;
  const auto num_tables = std::erase_if(
      tables, [&table_id](const TableInfoPtr& table) { return table->id() == table_id; });
  DCHECK_LE(num_tables, 1);
  if (tables.empty()) {
    LOG(INFO) << "Erasing DDL rollback to sub-transaction state " << txn_id;
    ysql_ddl_txn_undergoing_subtransaction_rollback_map_.erase(iter);
  } else {
    VLOG(1) << "DDL rollback to sub-transaction for " << txn_id << " has " << tables.size()
            << " tables remaining";
  }
}

void CatalogManager::RemoveDdlRollbackToSubTxnState(const TableId& table_id, TransactionId txn_id) {
  LockGuard lock(ddl_txn_verifier_mutex_);
  RemoveDdlRollbackToSubTxnStateUnlocked(table_id, txn_id);
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
              << ", txn_state: " << verifier_state->txn_state
              << ", state: " << verifier_state->state;
      return Status::OK();
    }

    if (verifier_state->txn_state == TxnState::kCommitted ||
        verifier_state->txn_state == TxnState::kAborted ||
        (verifier_state->txn_state == TxnState::kNoChange && verifier_state->tables.size() == 1)) {
      // (1) For kCommitted and kAborted, we already know whether this transaction is a success or
      // a failure.
      // (2) For kNoChange, if there is only one table involved then we know its DocDB
      // schema does not change so whether we treat it as kCommitted or kAborted does not matter.
      // In both cases, we don't need to poll the transaction coordinator at this point. We can
      // simply invoke post DDL verification directly.
      // When txn_state is kNoChange and there are multiple tables involved, we want to use
      // another table to do schema comparison which can yield kCommitted or kAborted.
      std::optional<bool> is_committed = std::nullopt;
      if (verifier_state->txn_state == TxnState::kCommitted) {
        is_committed = true;
      } else if (verifier_state->txn_state == TxnState::kAborted) {
        is_committed = false;
      }
      std::vector<TableId> table_ids;
      std::vector<TableId> remove_table_ids;
      for (const auto& table : verifier_state->tables) {
        table_ids.push_back(table->id());
        auto pb_txn_id = table->LockForRead()->pb_transaction_id();
        if (pb_txn_id.empty()) {
          // The table involved in ddl transaction txn_id has already finalized
          // with a new schema version, but verifier_state for txn_id isn't
          // cleared which implies the new schema version has not reached
          // all of its tablets yet. Call SendAlterTableRequestInternal to
          // sync them up. If fails, reschedule TriggerDdlVerificationIfNeeded
          // with a delay.
          auto s = SendAlterTableRequestInternal(table, TransactionId::Nil(), epoch);
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
          LOG(WARNING) << "pb_txn_id " << txn_id << " on table " << table->id()
                       << " differs from txn.transaction_id " << txn.transaction_id;
          remove_table_ids.push_back(table->id());
          continue;
        }
        return background_tasks_thread_pool_->SubmitFunc([this, table, pb_txn_id, is_committed,
                                                          epoch,
                                                          debug_caller_info = __FUNCTION__]() {
          WARN_NOT_OK(
              YsqlDdlTxnCompleteCallback(table, pb_txn_id, is_committed, epoch, debug_caller_info),
              Format("YsqlDdlTxnCompleteCallback failed, table: $0", table->id()));
        });
      }
      for (const auto& table_id : remove_table_ids) {
        RemoveDdlTransactionStateUnlocked(table_id, {txn.transaction_id});
      }
      VLOG(3) << "All tables " << VectorToString(table_ids) << " in transaction " << txn
              << " have pb_txn_id cleared or have a new txn_id";
      return Status::OK();
    }
    // Pick a table that is not in nochange_tables.
    for (size_t index = 0; index < verifier_state->tables.size(); ++index) {
      table = verifier_state->tables[index];
      if (!verifier_state->nochange_tables.contains(table->id())) {
        VLOG(3) << "Picked table at index " << index << " out of " << verifier_state->tables.size();
        break;
      }
    }
    // If none of the tables have schema change, pick the first table and use it
    // to complete the transaction.
    if (!table) {
      table = verifier_state->tables.front();
    }
  }

  // Schedule transaction verification.
  auto l = table->LockForRead();
  LOG(INFO) << "Enqueuing table for DDL transaction Verification: " << table->name()
            << " id: " << table->id() << " schema version: " << l->pb.version()
            << " for transaction " << txn;

  const string txn_id_pb = l->pb_transaction_id();
  auto when_done = [this, table, txn_id_pb, epoch](Result<std::optional<bool>> is_committed) {
    WARN_NOT_OK(YsqlTableSchemaChecker(table, txn_id_pb, is_committed, epoch),
                "YsqlTableSchemaChecker failed");
  };
  TableSchemaVerificationTask::CreateAndStartTask(
      *this, table, txn, std::move(when_done), sys_catalog_, master_->client_future(),
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

void CatalogManager::DoReleaseObjectLocksIfNecessary(const TransactionId& txn_id) {
  DEBUG_ONLY_TEST_SYNC_POINT("DoReleaseObjectLocksIfNecessary");
  object_lock_info_manager_->ReleaseLocksForTxn(txn_id);
}

Status CatalogManager::RollbackDocdbSchemaToSubtxn(
  const RollbackDocdbSchemaToSubtxnRequestPB* req,
  RollbackDocdbSchemaToSubtxnResponsePB* resp,
  rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  RSTATUS_DCHECK(req, InvalidArgument, "Received invalid RollbackDocdbSchemaToSubtxn request");
  const auto& req_txn = req->transaction_id();
  SCHECK(!req_txn.empty(), IllegalState,
      "Received RollbackDocdbSchemaToSubtxn request without transaction id");

  const auto& sub_txn_id = req->sub_transaction_id();
  SCHECK_GE(
      sub_txn_id, kMinSubTransactionId, IllegalState,
      Format(
          "Received RollbackDocdbSchemaToSubtxn request with an invalid sub-txn id $0",
          sub_txn_id));

  return YsqlRollbackDocdbSchemaToSubTxn(req_txn, sub_txn_id, epoch);
}

Status CatalogManager::YsqlRollbackDocdbSchemaToSubTxn(const std::string& pb_txn_id,
                                                  const SubTransactionId sub_txn_id,
                                                  const LeaderEpoch& epoch) {
  auto txn = VERIFY_RESULT(FullyDecodeTransactionId(pb_txn_id));
  LOG_WITH_FUNC(INFO) << "transaction: " << txn
                      << " sub_transaction: " << sub_txn_id;

  vector<TableInfoPtr> tables;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    auto verifier_state = FindOrNull(ysql_ddl_txn_verfication_state_map_, txn);
    if (!verifier_state || verifier_state->state != YsqlDdlVerificationState::kDdlInProgress ||
        verifier_state->txn_state != TxnState::kUnknown) {
      VLOG(3) << "Rolling back to sub-transaction for an already completed "
              << " transaction: " << txn
              << ", verifier_state: "
              << ((verifier_state) ? ToString(verifier_state->state) : "NULL")
              << ", txn_state: "
              << ((verifier_state) ? ToString(verifier_state->txn_state) : "NULL");
      return Status::OK();
    }

    auto sub_txn_rollback_state =
        FindOrNull(ysql_ddl_txn_undergoing_subtransaction_rollback_map_, txn);
    if (sub_txn_rollback_state) {
      RSTATUS_DCHECK(
          sub_txn_rollback_state->sub_txn == sub_txn_id, IllegalState,
          Format(
              "Rolling back to sub-transaction when another sub-transaction rollback in progress. "
              "transaction: $0, sub_transaction_id: $1, sub_transaction_id_in_progress: $2",
              txn, sub_txn_id, sub_txn_rollback_state->sub_txn));

      VLOG(3) << "Rolling back to sub-transaction is already in progress. "
              << " transaction: " << txn << ", sub_transaction_id: " << sub_txn_id;
      return Status::OK();
    }

    LOG(INFO) << "Enqueuing txn_id " << txn
              << ", sub_txn_id: " << sub_txn_id
              << " for rollback to sub-transaction operation.";
    ysql_ddl_txn_undergoing_subtransaction_rollback_map_.emplace(
        txn, YsqlDdlSubTransactionRollbackMetadata{
                                sub_txn_id,
                                YsqlDdlSubTransactionRollbackState::kDdlSubTxnRollbackInProgress,
                                {} /* tables */,
                                {} /* indexes_skipped_due_to_base_table_deletion */});

    tables = verifier_state->tables;
  }

  vector<TableInfoPtr> tables_to_trigger_rollback_for;
  for (auto& table : tables) {
    auto table_txn_id = table->LockForRead()->pb_transaction_id();

    // Deletion of a table is async. It is possible that the table was deleted in a previous
    // rollback to sub-transaction operation but before it could be removed from the
    // ysql_ddl_txn_verfication_state_map_ map, another RollbackDocdbSchemaToSubTxn RPC arrived.
    // In such cases, we should skip the rollback to sub-transaction for this table.
    if (table->LockForRead()->is_deleting() || table->LockForRead()->is_deleted()) {
      LOG(INFO) << "Skipping rollback to sub-transaction for table " << table->ToString()
                << " as it is being deleted or deleted";
      continue;
    }

    // If the table is no longer involved in a DDL transaction or involved in a new DDL transaction,
    // then txn has already completed.
    // This can happen if the transaction aborts (say client disconnect) during the processing of
    // the rollback to sub-transaction. There isn't much to do in this case. Any cleanup such as
    // removing from ysql_ddl_txn_undergoing_subtransaction_rollback_map_ map will be done by the
    // ysql_ddl_txn_verifier task which handles transaction aborts.
    if (table_txn_id.empty() || table_txn_id != pb_txn_id) {
      VLOG(3) << "Rolling back to sub-transaction for an already completed "
              << " transaction: " << txn
              << ", table: " << table->ToString()
              << ", table_txn_id: " << table_txn_id
              << ", pb_txn_id: " << pb_txn_id;
      return Status::OK();
    }

    bool is_table_index = false;
    if (table->is_index()) {
      // This is an index. If the indexed table is being deleted or marked for deletion due to the
      // sub-transaction rollback, then skip doing anything as the deletion of the table will delete
      // this index.
      const auto& indexed_table_id = table->indexed_table_id();
      auto indexed_table = VERIFY_RESULT(FindTableById(indexed_table_id));
      if (table->IsBeingDroppedDueToSubTxnRollback(pb_txn_id, sub_txn_id) &&
          indexed_table->IsBeingDroppedDueToSubTxnRollback(pb_txn_id, sub_txn_id)) {
        LOG(INFO) << "Will skip rollback to sub-transaction for index " << table->ToString()
                  << " as the indexed table " << indexed_table->ToString()
                  << " is also being dropped";
        is_table_index = true;
      }
    }

    {
      LOG(INFO) << "Adding table: " << table->ToString()
                << " to ysql_ddl_txn_undergoing_subtransaction_rollback_map_";
      LockGuard lock(ddl_txn_verifier_mutex_);
      if (is_table_index) {
        ysql_ddl_txn_undergoing_subtransaction_rollback_map_[txn]
            .indexes_skipped_due_to_base_table_deletion.insert(table->id());
        continue;
      }

      ysql_ddl_txn_undergoing_subtransaction_rollback_map_[txn].tables.push_back(table);
    }
    tables_to_trigger_rollback_for.push_back(table);
  }

  bool rollback_to_sub_txn_success = true;
  for (auto& table : tables_to_trigger_rollback_for) {
    auto s = background_tasks_thread_pool_->SubmitFunc([this, table, txn, sub_txn_id, epoch]() {
      auto s = YsqlRollbackDocdbSchemaToSubTxnHelper(table.get(), txn, sub_txn_id, epoch);
      if (!s.ok()) {
        LOG(WARNING) << "YsqlRollbackDocdbSchemaToSubTxnHelper failed for table "
                     << table->ToString()
                     << " txn " << txn << ": " << s.ToString();
        UpdateDdlRollbackToSubTxnState(
            txn, sub_txn_id,
            YsqlDdlSubTransactionRollbackState::kDdlSubTxnRollbackPostProcessingFailed);
      }
    });
    if (!s.ok()) {
      rollback_to_sub_txn_success = false;
      break;
    }
  }
  if (!rollback_to_sub_txn_success) {
    UpdateDdlRollbackToSubTxnState(
        txn, sub_txn_id,
        YsqlDdlSubTransactionRollbackState::kDdlSubTxnRollbackPostProcessingFailed);
    return STATUS_FORMAT(
        InternalError, "Failed to submit background task to rollback to sub-transaction");
  }

  return Status::OK();
}

Status CatalogManager::YsqlRollbackDocdbSchemaToSubTxnHelper(
  TableInfo* table, const TransactionId& txn_id, const SubTransactionId sub_txn_id,
  const LeaderEpoch& epoch) {
  const auto id = "table id: " + table->id();

  auto l = table->LockForWrite();
  if (!VERIFY_RESULT(l->is_being_modified_by_ddl_transaction(txn_id))) {
    // Transaction is already complete for this table. This could happen if the tserver / PG crashes
    // immediately after the savepoint rollback leading to the transaction abort.
    VLOG(3) << "YsqlRollbackDocdbSchemaToSubTxnHelper called for an already finished txn: "
            << txn_id << ", sub_txn: " << sub_txn_id << ", table: " << id << ". Ignoring";
    RemoveDdlRollbackToSubTxnState(table->id(), txn_id);
    return Status::OK();
  }
  if (!l->is_running()) {
    // This table is not in a running state. Skip the rollback operation for this table.
    // This could happen if the table was deleted as part of a previous rollback operation but is
    // not yet removed from the ysql_ddl_txn_verfication_state_map_ which only happens upon
    // transaction abort / commit.
    VLOG(3) << "YsqlRollbackDocdbSchemaToSubTxnHelper called for table that isn't running."
            << txn_id << ", sub_txn: " << sub_txn_id << ", table: " << id << ". Ignoring";
    RemoveDdlRollbackToSubTxnState(table->id(), txn_id);
    return Status::OK();
  }

  LOG_WITH_FUNC(INFO) << id << " for transaction " << txn_id
                      << ", sub_transaction: " << sub_txn_id
                      << ", ysql_ddl_txn_verifier_state: "
                      << CollectionToString(l->ysql_ddl_txn_verifier_state());

  // Find the index of ddl_state in ysql_ddl_txn_verifier_state with smallest sub-transaction id >=
  // sub_txn_id we have to rollback to.
  //
  // Example:
  //    BEGIN;
  //    SAVEPOINT a;                          -- sub_transaction_id = 2
  //    ALTER TABLE test ADD COLUMN c1 INT;
  //    SAVEPOINT b;                          -- sub_transaction_id = 3
  //    ALTER TABLE test2 ADD COLUMN c2 INT;
  //    SAVEPOINT c;                          -- sub_transaction_id = 4
  //    ALTER TABLE test ADD COLUMN c3 INT;
  //    ROLLBACK TO SAVEPOINT b;              -- Rollback all the DDLs till sub_transaction_id = 3
  //
  // For the table test, the ysql_ddl_txn_verifier_state will look like:
  // [
  //   {alter_table_op: true, .., sub_transaction_id = 2},
  //   {alter_table_op: true, .., sub_transaction_id = 4}
  // ]
  //
  // In the above example, we have to rollback to the state with sub_transaction_id = 3 i.e. for
  // table 'test', we must rollback the changes made by sub_transaction_id = 4.
  // So rollback_till_ddl_state_index must be 1 in this case (zero based index).
  const auto& ddl_states = l->ysql_ddl_txn_verifier_state();
  int rollback_till_ddl_state_index = l->ysql_first_ddl_state_at_or_after_sub_txn(sub_txn_id);
  if (rollback_till_ddl_state_index == ddl_states.size()) {
    // Example:
    //    BEGIN;
    //    ALTER TABLE test ADD COLUMN c INT;
    //    SAVEPOINT a;                          -- sub_transaction_id = 2
    //    ALTER TABLE test2 ADD COLUMN c INT;
    //    ROLLBACK TO SAVEPOINT a;
    //
    // When rolling back to savepoint a, there won't be anything to rollback for table 'test'.
    VLOG_WITH_FUNC(3) << "Nothing to rollback to for transaction: "
      << txn_id << ", sub_transaction: "
      << sub_txn_id << ", table: "
      << id << ", ddl_states: "
      << CollectionToString(ddl_states);
    RemoveDdlRollbackToSubTxnState(table->id(), txn_id);
    return Status::OK();
  }

  VLOG_WITH_FUNC(3) << "rollback_till_ddl_state_index: "
          << rollback_till_ddl_state_index << ", ddl_states: "
          << CollectionToString(ddl_states);

  auto txn_data = YsqlTableDdlTxnState {
    .table = table,
    .write_lock = l,
    .epoch = epoch,
    .ddl_txn_id = txn_id
  };

  return RollbackYsqlTxnDdlStates(
      txn_data, true /* is_rollback_to_subtxn */, rollback_till_ddl_state_index);
}

Status CatalogManager::IsRollbackDocdbSchemaToSubtxnDone(
    const IsRollbackDocdbSchemaToSubtxnDoneRequestPB* req,
    IsRollbackDocdbSchemaToSubtxnDoneResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  auto txn = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
  const auto& req_txn = txn.transaction_id;
  const auto& sub_txn_id = req->sub_transaction_id();
  SCHECK_GE(sub_txn_id, kMinSubTransactionId, IllegalState,
    Format("Received IsRollbackDocdbSchemaToSubtxnDone request with an invalid sub-txn id $0",
           sub_txn_id));

  bool is_done = false;
  bool is_success = false;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    const auto itr = ysql_ddl_txn_undergoing_subtransaction_rollback_map_.find(req_txn);
    // Rollback is done if:
    // 1. We don't have the state - indicates that the state was cleaned up after a successful
    // rollback.
    // 2. We have the state but it corresponds to a new sub-txn - rollback was successful and then
    // rollback to a different sub-transaction is in progress.
    // 3. The saved state is "failed" - indicates a failure during the rollback operation.
    is_done = itr == ysql_ddl_txn_undergoing_subtransaction_rollback_map_.end() ||
              itr->second.sub_txn != sub_txn_id ||
              itr->second.state ==
                  YsqlDdlSubTransactionRollbackState::kDdlSubTxnRollbackPostProcessingFailed;

    // Condition 1 and 2 of the is_done check.
    is_success = itr == ysql_ddl_txn_undergoing_subtransaction_rollback_map_.end() ||
                 itr->second.sub_txn != sub_txn_id;
  }
  resp->set_done(is_done);
  VLOG(1) << "Received IsRollbackDocdbSchemaToSubtxnDoneRequestPB request for transaction "
          << req_txn << ", sub_transaction_id: " << sub_txn_id
          << " responding with " << (is_done ? "true" : "false")
          << ", is_success: " << (is_success ? "true" : "false");
  return (is_done && !is_success)
             ? STATUS_FORMAT(InternalError, "Rollback to sub-transaction failed.")
             : Status::OK();
}

bool CatalogManager::IsTableDeletionDueToRollbackToSubTxn(
    const scoped_refptr<TableInfo>& table, TransactionId& txn_id) {
  if (!FLAGS_ysql_yb_enable_ddl_savepoint_support) {
    return false;
  }

  // Get the transaction id (if exists) that is modifying the table.
  {
    auto l = table->LockForRead();
    auto txn_id_res = l->GetCurrentDdlTransactionId();
    if (!txn_id_res.ok() || txn_id_res->IsNil()) {
      // Transaction ID will always be present in case of rollback to sub-transaction operation.
      return false;
    }
    txn_id = *txn_id_res;
  }

  VLOG_WITH_FUNC(4) << "The transaction modifying the table: " << table->ToString()
                    << " is " << txn_id;

  SubTransactionId rolled_back_sub_transaction_id;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    const auto rollback_to_subtxn_state =
        FindOrNull(ysql_ddl_txn_undergoing_subtransaction_rollback_map_, txn_id);
    if (rollback_to_subtxn_state == nullptr) {
      VLOG_WITH_FUNC(4) << "No rollback to sub-transaction in progress for transaction: " << txn_id;
      return false;
    }

    // For an index, we skip the deletion of index directly via rollback to sub-transaction
    // operation when the main table is also getting deleted. See the
    // YsqlRollbackDocdbSchemaToSubTxn function above.
    // So, in such a case, we need to check in indexes_skipped_due_to_base_table_deletion.
    if (table->is_index() &&
        rollback_to_subtxn_state->indexes_skipped_due_to_base_table_deletion.contains(
            table->id())) {
      VLOG_WITH_FUNC(4) << "Table: " << table->ToString()
                        << " is an index whose base table is also getting deleted due to rollback "
                           "to sub-transaction operation.";
      return true;
    }

    auto tables_itr = std::find_if(
        rollback_to_subtxn_state->tables.begin(), rollback_to_subtxn_state->tables.end(),
        [&table](const TableInfoPtr& table_info) {
          return table_info->id() == table->id(); });
    if (tables_itr == rollback_to_subtxn_state->tables.end()) {
      // This table is not undergoing rollback to sub-transaction operation.
      // Either it has already completed or it wasn't affected.
      VLOG_WITH_FUNC(4) << "No rollback to sub-transaction in progress for table: "
                        << table->ToString();
      return false;
    }

    rolled_back_sub_transaction_id = rollback_to_subtxn_state->sub_txn;
  }

  auto l = table->LockForRead();
  // Ensure this table is still being modified by the same transaction id.
  auto txn_id_res = l->GetCurrentDdlTransactionId();
  if (!txn_id_res.ok() || txn_id_res->IsNil() || txn_id != *txn_id_res) {
    // Table is now:
    // 1. Not under any DDL anymore i.e. completed
    // 2. Undergoing DDL via a different transaction
    // None of these cases should happen because this would indicate that the transaction finished
    // and got cleaned up even before the table deletion finished.
    // Crash in DEBUG so that we can find out cases where this is happening.
    // Return false in RELEASE to avoid crash. There is no correctness issue with returning false as
    // this function is used to avoid self-abort of transaction. When we return false, no txn are
    // excluded i.e. all get aborted as part of tablet deletion.
    LOG(DFATAL) << "DDL transaction id for table: " << table
                << " changed while DeleteTable was in progress. old: " << txn_id
                << ", new: " << txn_id_res;
    return false;
  }

  // The table must be created within or after the rolled back sub-transaction.
  const YsqlDdlTxnVerifierStatePB& first_ddl_state = l->ysql_ddl_txn_verifier_state_first();
  return first_ddl_state.contains_create_table_op() &&
         first_ddl_state.has_sub_transaction_id() &&
         first_ddl_state.sub_transaction_id() >= rolled_back_sub_transaction_id;
}

} // namespace yb::master
