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

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/ysql_transaction_ddl.h"

DEFINE_test_flag(bool, disable_ysql_ddl_txn_verification, false,
    "Simulates a condition where the background process that checks whether the YSQL transaction "
    "was a success or a failure is indefinitely delayed");

DEFINE_test_flag(int32, delay_ysql_ddl_rollback_secs, 0,
                 "Number of seconds to sleep before rolling back a failed ddl transaction");

DEFINE_test_flag(int32, ysql_max_random_delay_before_ddl_verification_usecs, 0,
                  "Maximum #usecs to randomly sleep before verifying a YSQL DDL transaction");

using namespace std::placeholders;
using std::shared_ptr;
using std::string;
using std::vector;

namespace yb {
namespace master {

void CatalogManager::ScheduleYsqlTxnVerification(
    const scoped_refptr<TableInfo>& table, const TransactionMetadata& txn,
    const LeaderEpoch& epoch) {
  // Add this transaction to the map containing all the transactions yet to be
  // verified.
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    ddl_txn_id_to_table_map_[txn.transaction_id].push_back(table);
  }

  if (FLAGS_TEST_disable_ysql_ddl_txn_verification) {
    LOG(INFO) << "Skip scheduling table " << table->ToString() << " for transaction verification "
              << "as TEST_disable_ysql_ddl_txn_verification is set";
    return;
  }

  // Schedule transaction verification.
  auto l = table->LockForRead();
  LOG(INFO) << "Enqueuing table for DDL transaction Verification: " << table->name()
            << " id: " << table->id() << " schema version: " << l->pb.version()
            << " for transaction " << txn;
  std::function<Status(bool)> when_done =
    std::bind(&CatalogManager::YsqlTableSchemaChecker, this, table,
              l->pb_transaction_id(), _1, epoch);
  // For now, just print warning if submission to thread pool fails. Fix this as part of
  // #13358.
  WARN_NOT_OK(background_tasks_thread_pool_->SubmitFunc(
    std::bind(&YsqlTransactionDdl::VerifyTransaction, ysql_transaction_.get(), txn, table,
              true /* has_ysql_ddl_state */, when_done)),
      "Could not submit VerifyTransaction to thread pool");
}

Status CatalogManager::YsqlTableSchemaChecker(
    scoped_refptr<TableInfo> table, const string& txn_id_pb, bool txn_rpc_success,
    const LeaderEpoch& epoch) {
  if (!txn_rpc_success) {
    return STATUS_FORMAT(IllegalState, "Failed to find Transaction Status for table $0",
                         table->ToString());
  }
  bool is_committed = VERIFY_RESULT(ysql_transaction_->PgSchemaChecker(table));
  return YsqlDdlTxnCompleteCallback(table, txn_id_pb, is_committed, epoch);
}

Status CatalogManager::ReportYsqlDdlTxnStatus(
    const ReportYsqlDdlTxnStatusRequestPB* req, ReportYsqlDdlTxnStatusResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  DCHECK(req);
  const auto& req_txn = req->transaction_id();
  SCHECK(!req_txn.empty(), IllegalState,
      "Received ReportYsqlDdlTxnStatus request without transaction id");
  auto txn = VERIFY_RESULT(FullyDecodeTransactionId(req_txn));

  const auto is_committed = req->is_committed();
  LOG(INFO) << "Received ReportYsqlDdlTxnStatus request for transaction " << txn
            << ". Status: " << (is_committed ? "Success" : "Aborted");
  {
    SharedLock lock(ddl_txn_verifier_mutex_);
    const auto iter = ddl_txn_id_to_table_map_.find(txn);
    if (iter == ddl_txn_id_to_table_map_.end()) {
      // Transaction not found in the list of transactions to be verified. Ideally this means that
      // the YB-Master background task somehow got to it before PG backend sent this report. However
      // it is possible to receive this report BEFORE we added the transaction to the map if:
      // 1. The transaction failed before performing any DocDB schema change.
      // 2. Transaction failed and this report arrived in the small window between schema change
      //    initiation and scheduling the verification task.
      // We have to do nothing in case of (1). In case of (2), it is safe to do nothing as the
      // background task will take care of it. This is not optimal but (2) is expected to be very
      // rare.
      LOG(INFO) << "DDL transaction " << txn << " not found in list of transactions to be "
                << "verified, nothing to do";
      return Status::OK();
    }
    for (const auto& table : iter->second) {
      // Submit this table for transaction verification.
      LOG(INFO) << "Enqueuing table " << table->ToString() << " for handling "
                << (is_committed ? "successful " : "aborted ")  << txn;
      WARN_NOT_OK(
          background_tasks_thread_pool_->SubmitFunc([this, table, req_txn, is_committed, epoch]() {
            WARN_NOT_OK(
                YsqlDdlTxnCompleteCallback(table, req_txn, is_committed, epoch),
                "Transaction verification failed for table " + table->ToString());
          }),
          "Could not submit YsqlDdlTxnCompleteCallback to thread pool");
    }
  }
  return Status::OK();
}

Status CatalogManager::YsqlDdlTxnCompleteCallback(scoped_refptr<TableInfo> table,
                                                  const string& txn_id_pb,
                                                  bool success,
                                                  const LeaderEpoch& epoch) {
  SleepFor(MonoDelta::FromMicroseconds(RandomUniformInt<int>(0,
    FLAGS_TEST_ysql_max_random_delay_before_ddl_verification_usecs)));

  DCHECK(!txn_id_pb.empty());
  DCHECK(table);
  const auto& table_id = table->id();
  const auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(txn_id_pb));
  bool table_present = false;
  {
    LockGuard lock(ddl_txn_verifier_mutex_);
    const auto iter = ddl_txn_id_to_table_map_.find(txn_id);
    if (iter == ddl_txn_id_to_table_map_.end()) {
      LOG(INFO) << "DDL transaction " << txn_id << " for table " << table->ToString()
                << " is already verified, ignoring";
      return Status::OK();
    }

    auto& tables = iter->second;
    auto removed_elements_iter = std::remove_if(tables.begin(), tables.end(),
        [&table_id](const scoped_refptr<TableInfo>& table) {
      return table->id() == table_id;
    });
    if (removed_elements_iter != tables.end()) {
      tables.erase(removed_elements_iter, tables.end());
      table_present = true;
      if (tables.empty()) {
        ddl_txn_id_to_table_map_.erase(iter);
      }
    }
  }
  if (!table_present) {
    LOG(INFO) << "DDL transaction " << txn_id << " for table " << table->ToString()
              << " is already verified, ignoring";
    return Status::OK();
  }
  if (table->is_index()) {
    // This is an index. If the indexed table is being deleted or marked for deletion, then skip
    // doing anything as the deletion of the table will delete this index.
    const auto& indexed_table_id = table->indexed_table_id();
    auto indexed_table = VERIFY_RESULT(FindTableById(indexed_table_id));
    if (table->IsBeingDroppedDueToDdlTxn(txn_id_pb, success) &&
        indexed_table->IsBeingDroppedDueToDdlTxn(txn_id_pb, success)) {
      VLOG(1) << "Skipping DDL transaction verification for index " << table->ToString()
              << " as the indexed table " << indexed_table->ToString()
              << " is also being dropped";
      return Status::OK();
    }
  }
  return YsqlDdlTxnCompleteCallbackInternal(table.get(), txn_id, success, epoch);
}

Status CatalogManager::YsqlDdlTxnCompleteCallbackInternal(
    TableInfo* table, const TransactionId& txn_id, bool success, const LeaderEpoch& epoch) {
  if (FLAGS_TEST_delay_ysql_ddl_rollback_secs > 0) {
    LOG(INFO) << "YsqlDdlTxnCompleteCallbackInternal: Sleep for "
              << FLAGS_TEST_delay_ysql_ddl_rollback_secs << " seconds";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_delay_ysql_ddl_rollback_secs));
  }
  const auto id = "table id: " + table->id();

  auto l = table->LockForWrite();
  if (!VERIFY_RESULT(l->is_being_modified_by_ddl_transaction(txn_id))) {
    // Transaction verification completed for this table.
    LOG(INFO) << "Verification of transaction " << txn_id << " for " << id
              << " is already complete, ignoring";
    return Status::OK();
  }
  LOG(INFO) << "YsqlDdlTxnCompleteCallback for " << id
            << " for transaction " << txn_id
            << ": Success: " << (success ? "true" : "false")
            << " ysql_ddl_txn_verifier_state: "
            << l->ysql_ddl_txn_verifier_state().DebugString();

  auto& metadata = l.mutable_data()->pb;

  SCHECK(metadata.state() == SysTablesEntryPB::RUNNING ||
         metadata.state() == SysTablesEntryPB::ALTERING, Aborted,
         "Unexpected table state ($0), abandoning DDL rollback for $1",
         SysTablesEntryPB_State_Name(metadata.state()), table->ToString());

  if (success) {
    RETURN_NOT_OK(HandleSuccessfulYsqlDdlTxn(table, &l, epoch));
  } else {
    RETURN_NOT_OK(HandleAbortedYsqlDdlTxn(table, &l, epoch));
  }
  return Status::OK();
}

Status CatalogManager::HandleSuccessfulYsqlDdlTxn(
    TableInfo* const table, TableInfo::WriteLock* l, const LeaderEpoch& epoch) {
  // The only DDL operations that roll-forward (i.e. take complete effect after commit) are DROP
  // TABLE and DROP COLUMN.
  if ((*l)->is_being_deleted_by_ysql_ddl_txn()) {
    return YsqlDdlTxnDropTableHelper(table, l, epoch);
  }

  vector<string> cols_being_dropped;
  auto& mutable_pb = l->mutable_data()->pb;
  for (const auto& col : mutable_pb.schema().columns()) {
    if (col.marked_for_deletion()) {
        cols_being_dropped.push_back(col.name());
    }
  }
  if (cols_being_dropped.empty()) {
    return ClearYsqlDdlTxnState(table, l, epoch);
  }
  Schema current_schema;
  RETURN_NOT_OK(SchemaFromPB(mutable_pb.schema(), &current_schema));
  SchemaBuilder builder(current_schema);
  std::vector<DdlLogEntry> ddl_log_entries;

  for (const auto& col : cols_being_dropped) {
    RETURN_NOT_OK(builder.RemoveColumn(col));
    ddl_log_entries.emplace_back(
          master_->clock()->Now(),
          table->id(),
          mutable_pb,
          Format("Drop column $0", col));
  }
  SchemaToPB(builder.Build(), mutable_pb.mutable_schema());
  return YsqlDdlTxnAlterTableHelper(table, l, ddl_log_entries, "" /* new_table_name */, epoch);
}

Status CatalogManager::HandleAbortedYsqlDdlTxn(TableInfo *const table,
                                               TableInfo::WriteLock* l,
                                               const LeaderEpoch& epoch) {
  auto& mutable_pb = l->mutable_data()->pb;
  const auto& ddl_state = mutable_pb.ysql_ddl_txn_verifier_state(0);
  if (ddl_state.contains_create_table_op()) {
    // This table was created in this aborted transaction. Drop this table.
    return YsqlDdlTxnDropTableHelper(table, l, epoch);
  }
  if (ddl_state.contains_alter_table_op()) {
    LOG(INFO) << "Alter transaction on " << table->id()
              << " failed, rolling back its schema changes";
    std::vector<DdlLogEntry> ddl_log_entries;
    ddl_log_entries.emplace_back(
        master_->clock()->Now(),
        table->id(),
        mutable_pb,
        "Rollback of DDL Transaction");
    mutable_pb.mutable_schema()->CopyFrom(ddl_state.previous_schema());
    const string new_table_name = ddl_state.previous_table_name();
    mutable_pb.set_name(new_table_name);
    return YsqlDdlTxnAlterTableHelper(table, l, ddl_log_entries, new_table_name, epoch);
  }

  // This must be a failed Delete transaction.
  DCHECK(ddl_state.contains_drop_table_op());
  return ClearYsqlDdlTxnState(table, l, epoch);
}

Status CatalogManager::ClearYsqlDdlTxnState(
    TableInfo* table, TableInfo::WriteLock* l, const LeaderEpoch& epoch) {
  auto& pb = l->mutable_data()->pb;
  LOG(INFO) << "Clearing ysql_ddl_txn_verifier_state from table " << table->id();
  pb.clear_ysql_ddl_txn_verifier_state();
  pb.clear_transaction();
  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
  l->Commit();
  return Status::OK();
}

Status CatalogManager::YsqlDdlTxnAlterTableHelper(TableInfo *table,
                                                  TableInfo::WriteLock* l,
                                                  const std::vector<DdlLogEntry>& ddl_log_entries,
                                                  const string& new_table_name,
                                                  const LeaderEpoch& epoch) {
  auto& table_pb = l->mutable_data()->pb;
  table_pb.set_version(table_pb.version() + 1);
  table_pb.set_updates_only_index_permissions(false);
  table_pb.set_state(SysTablesEntryPB::ALTERING);
  table_pb.set_state_msg(
    strings::Substitute("Alter table version=$0 ts=$1", table_pb.version(), LocalTimeAsString()));

  table_pb.clear_ysql_ddl_txn_verifier_state();
  table_pb.clear_transaction();

  // Update sys-catalog with the new table schema.
  RETURN_NOT_OK(UpdateSysCatalogWithNewSchema(
        table,
        ddl_log_entries,
        "" /* new_namespace_id */,
        new_table_name,
        epoch,
        nullptr /* resp */));
  l->Commit();
  LOG(INFO) << "Sending Alter Table request as part of rollback for table " << table->name();
  return SendAlterTableRequestInternal(table, TransactionId::Nil(), epoch);
}

Status CatalogManager::YsqlDdlTxnDropTableHelper(
    TableInfo* table, TableInfo::WriteLock* l, const LeaderEpoch& epoch) {
  LOG(INFO) << "Dropping " << table->ToString();
  l->Commit();
  DeleteTableRequestPB dtreq;
  DeleteTableResponsePB dtresp;

  dtreq.mutable_table()->set_table_name(table->name());
  dtreq.mutable_table()->set_table_id(table->id());
  dtreq.set_is_index_table(table->is_index());
  return DeleteTableInternal(&dtreq, &dtresp, nullptr /* rpc */, epoch);
}

} // namespace master
} // namespace yb
