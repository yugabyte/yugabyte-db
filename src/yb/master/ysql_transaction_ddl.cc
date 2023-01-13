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
//

#include "yb/master/ysql_transaction_ddl.h"

#include "yb/client/transaction_rpc.h"

#include "yb/common/ql_expr.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_int32(ysql_transaction_bg_task_wait_ms, 200,
  "Amount of time the catalog manager background task thread waits "
  "between runs");

using std::string;
using std::vector;

namespace yb {
namespace master {

YsqlTransactionDdl::~YsqlTransactionDdl() {
  // Shutdown any outstanding RPCs.
  rpcs_.Shutdown();
}

Result<bool> YsqlTransactionDdl::PgEntryExists(TableId pg_table_id, Result<uint32_t> entry_oid,
                                               TableId relfilenode_oid) {
  vector<GStringPiece> col_names = {"oid"};
  bool is_matview = relfilenode_oid.empty() ? false : true;
  if (is_matview) {
    col_names.emplace_back("relfilenode");
  }

  Schema projection;
  auto iter = VERIFY_RESULT(GetPgCatalogTableScanIterator(pg_table_id,
        "oid", VERIFY_RESULT(std::move(entry_oid)), std::move(col_names), &projection));

  // If no rows found, the entry does not exist.
  if (!VERIFY_RESULT(iter->HasNext())) {
    return false;
  }

  // The entry exists. Expect only one row.
  QLTableRow row;
  RETURN_NOT_OK(iter->NextRow(&row));
  CHECK(!VERIFY_RESULT(iter->HasNext()));
  if (is_matview) {
    const auto relfilenode_col_id = VERIFY_RESULT(projection.ColumnIdByName("relfilenode")).rep();
    const auto& relfilenode = row.GetValue(relfilenode_col_id);
    if (relfilenode->uint32_value() != VERIFY_RESULT(GetPgsqlTableOid(relfilenode_oid))) {
      return false;
    }
  }
  return true;
}

Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>>
YsqlTransactionDdl::GetPgCatalogTableScanIterator(const TableId& pg_catalog_table_id,
                                                  const string& oid_col_name,
                                                  uint32_t oid_value,
                                                  std::vector<GStringPiece> col_names,
                                                  Schema *projection) {

  auto tablet_peer = sys_catalog_->tablet_peer();
  if (!tablet_peer || !tablet_peer->tablet()) {
    return STATUS(ServiceUnavailable, "SysCatalog unavailable");
  }
  const tablet::Tablet* catalog_tablet = tablet_peer->tablet();
  DCHECK(catalog_tablet->metadata());
  const Schema& schema =
      VERIFY_RESULT(catalog_tablet->metadata()->GetTableInfo(pg_catalog_table_id))->schema();

  // Use Scan to query the given table, filtering by lookup_oid_col.
  RETURN_NOT_OK(schema.CreateProjectionByNames(col_names, projection, schema.num_key_columns()));
  const auto oid_col_id = VERIFY_RESULT(projection->ColumnIdByName(oid_col_name)).rep();
  auto iter = VERIFY_RESULT(catalog_tablet->NewRowIterator(
      projection->CopyWithoutColumnIds(), {} /* read_hybrid_time */, pg_catalog_table_id));

  auto doc_iter = down_cast<docdb::DocRowwiseIterator*>(iter.get());
  PgsqlConditionPB cond;
  cond.add_operands()->set_column_id(oid_col_id);
  cond.set_op(QL_OP_EQUAL);
  cond.add_operands()->mutable_value()->set_uint32_value(oid_value);
  const std::vector<docdb::KeyEntryValue> empty_key_components;
  docdb::DocPgsqlScanSpec spec(
      *projection, rocksdb::kDefaultQueryId, empty_key_components, empty_key_components,
      &cond, boost::none /* hash_code */, boost::none /* max_hash_code */, nullptr /* where */);
  RETURN_NOT_OK(doc_iter->Init(spec));
  return iter;
}

void YsqlTransactionDdl::VerifyTransaction(
    const TransactionMetadata& transaction_metadata,
    scoped_refptr<TableInfo> table,
    bool has_ysql_ddl_txn_state,
    std::function<Status(bool)> complete_callback) {

  SleepFor(MonoDelta::FromMilliseconds(FLAGS_ysql_transaction_bg_task_wait_ms));

  YB_LOG_EVERY_N_SECS(INFO, 1) << "Verifying Transaction " << transaction_metadata;

  tserver::GetTransactionStatusRequestPB req;
  req.set_tablet_id(transaction_metadata.status_tablet);
  req.add_transaction_id()->assign(
      pointer_cast<const char*>(transaction_metadata.transaction_id.data()),
      transaction_metadata.transaction_id.size());

  auto rpc_handle = rpcs_.Prepare();
  if (rpc_handle == rpcs_.InvalidHandle()) {
    LOG(WARNING) << "Shutting down. Cannot send GetTransactionStatus: " << transaction_metadata;
    return;
  }
  auto client = client_future_.get();
  if (!client) {
    LOG(WARNING) << "Shutting down. Cannot get GetTransactionStatus: " << transaction_metadata;
    return;
  }
  // We need to query the TransactionCoordinator here.  Can't use TransactionStatusResolver in
  // TransactionParticipant since this TransactionMetadata may not have any actual data flushed yet.
  *rpc_handle = client::GetTransactionStatus(
    TransactionRpcDeadline(),
    nullptr /* tablet */,
    client,
    &req,
    [this, rpc_handle, transaction_metadata, table, has_ysql_ddl_txn_state, complete_callback]
        (Status status, const tserver::GetTransactionStatusResponsePB& resp) {
      auto retained = rpcs_.Unregister(rpc_handle);
      TransactionReceived(transaction_metadata, table, has_ysql_ddl_txn_state,
          complete_callback, std::move(status), resp);
    });
  (**rpc_handle).SendRpc();
}

void YsqlTransactionDdl::TransactionReceived(
    const TransactionMetadata& transaction,
    scoped_refptr<TableInfo> table,
    bool has_ysql_ddl_txn_state,
    std::function<Status(bool)> complete_callback,
    Status txn_status, const tserver::GetTransactionStatusResponsePB& resp) {
  if (has_ysql_ddl_txn_state) {
    // This was invoked for a table for which YSQL DDL rollback is enabled. Verify that it
    // contains ysql_ddl_txn_state even now.
    DCHECK(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
  }

  if (!txn_status.ok()) {
    LOG(WARNING) << "Transaction Status attempt (" << transaction.ToString()
                 << ") failed with status " << txn_status;
    WARN_NOT_OK(thread_pool_->SubmitFunc([complete_callback] () {
      WARN_NOT_OK(complete_callback(false /* txn_rpc_success */), "Callback failure");
    }), "Failed to enqueue callback");
    return;
    // #5981: Improve failure handling to retry transient errors or recognize transaction complete.
  }
  if (resp.has_error()) {
    const Status s = StatusFromPB(resp.error().status());
    const tserver::TabletServerErrorPB::Code code = resp.error().code();
    LOG(WARNING) << "Transaction Status attempt (" << transaction.ToString()
                 << ") failed with error code " << tserver::TabletServerErrorPB::Code_Name(code)
                 << ": " << s;
    WARN_NOT_OK(thread_pool_->SubmitFunc([complete_callback] () {
      WARN_NOT_OK(complete_callback(false /* txn_rpc_success */), "Callback failure");
    }), "Failed to enqueue callback");
    // #5981: Maybe have the same heuristic as above?
    return;
  }
  YB_LOG_EVERY_N_SECS(INFO, 1) << "Got Response for " << transaction.ToString()
                               << ", resp: " << resp.ShortDebugString();
  bool is_pending = (resp.status_size() == 0);
  for (int i = 0; i < resp.status_size() && !is_pending; ++i) {
    // NOTE: COMMITTED state is also "pending" because we need APPLIED.
    is_pending = resp.status(i) == TransactionStatus::PENDING ||
                 resp.status(i) == TransactionStatus::COMMITTED;
  }
  if (is_pending) {
    // Re-enqueue if transaction is still pending.
    WARN_NOT_OK(thread_pool_->SubmitFunc(
        std::bind(&YsqlTransactionDdl::VerifyTransaction, this,
                  transaction, table, has_ysql_ddl_txn_state, complete_callback)),
        "Could not submit VerifyTransaction to thread pool");
    return;
  }
  // If this transaction isn't pending, then the transaction is in a terminal state.
  // Note: We ignore the resp.status() now, because it could be ABORT'd but actually a SUCCESS.
  // Determine whether the transaction was a success by comparing with the PG schema.
  WARN_NOT_OK(thread_pool_->SubmitFunc([complete_callback] () {
    WARN_NOT_OK(complete_callback(true /* txn_rpc_success */), "Callback failure");
  }), "Failed to enqueue callback");
}

Result<bool> YsqlTransactionDdl::PgSchemaChecker(const scoped_refptr<TableInfo>& table) {
  const uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTableId(table->id()));
  const auto& pg_catalog_table_id = GetPgsqlTableId(database_oid, kPgClassTableOid);

  Schema projection;
  uint32_t oid = VERIFY_RESULT(GetPgsqlTableOid(table->id()));
  auto iter = VERIFY_RESULT(GetPgCatalogTableScanIterator(pg_catalog_table_id,
                                                          "oid" /* oid_col_name */,
                                                          oid,
                                                          {"oid", "relname"},
                                                          &projection));

  QLTableRow row;
  auto l = table->LockForRead();
  if (!l->has_ysql_ddl_txn_verifier_state()) {
    // The table no longer has transaction verifier state on it, it was probably cleaned up
    // concurrently.
    return STATUS_FORMAT(Aborted, "Not performing transaction verification for table $0 as it no "
                         "longer has any transaction verification state", table->ToString());
  }
  // Table not found in pg_class. This can only happen in two cases: Table creation failed,
  // or a table deletion went through successfully.
  if (!VERIFY_RESULT(iter->HasNext())) {
    if (l->is_being_deleted_by_ysql_ddl_txn()) {
      return true;
    }
    CHECK(l->is_being_created_by_ysql_ddl_txn());
    return false;
  }

  // Table found in pg_class.
  if (l->is_being_deleted_by_ysql_ddl_txn()) {
    LOG(INFO) << "Ysql Drop transaction for " << table->ToString()
              << " detected to have failed as table found "
              << "in PG catalog";
    return false;
  }

  if (l->is_being_created_by_ysql_ddl_txn()) {
    return true;
  }

  // Table was being altered. Check whether its current DocDB schema matches
  // that of PG catalog.
  CHECK(l->ysql_ddl_txn_verifier_state().contains_alter_table_op());
  RETURN_NOT_OK(iter->NextRow(&row));
  const auto relname_col_id = VERIFY_RESULT(projection.ColumnIdByName("relname")).rep();
  const auto& relname_col = row.GetValue(relname_col_id);
  const string& table_name = relname_col->string_value();

  const string fail_msg = "Alter transaction on " + table->ToString() + " failed.";
  if (table->name().compare(table_name) != 0) {
    // Table name does not match.
    LOG(INFO) << fail_msg << Format(" Expected table name: $0 Table name in PG: $1",
        table->name(), table_name);
    CHECK_EQ(table_name, l->ysql_ddl_txn_verifier_state().previous_table_name());
    return false;
  }

  vector<YsqlTransactionDdl::PgColumnFields> pg_cols = VERIFY_RESULT(ReadPgAttribute(table));
  // In DocDB schema, columns are sorted based on 'order'.
  sort(pg_cols.begin(), pg_cols.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.order < rhs.order;
  });

  Schema schema;
  RETURN_NOT_OK(table->GetSchema(&schema));
  if (MatchPgDocDBSchemaColumns(table, schema, pg_cols)) {
    // The PG catalog schema matches the current DocDB schema. The transaction was a success.
    return true;
  }

  Schema previous_schema;
  RETURN_NOT_OK(SchemaFromPB(l->ysql_ddl_txn_verifier_state().previous_schema(), &previous_schema));
  if (MatchPgDocDBSchemaColumns(table, previous_schema, pg_cols)) {
    // The PG catalog schema matches the DocDB schema of the table prior to this transaction. The
    // transaction must have aborted.
    return false;
  }

  // The PG catalog schema does not match either the current schema nor the previous schema. This
  // is an unexpected state, do nothing.
  return STATUS_FORMAT(IllegalState, "Failed to verify transaction for table $0",
                       table->ToString());
}

bool YsqlTransactionDdl::MatchPgDocDBSchemaColumns(
  const scoped_refptr<TableInfo>& table,
  const Schema& schema,
  const vector<YsqlTransactionDdl::PgColumnFields>& pg_cols) {

  const string& fail_msg = "Schema mismatch for table " + table->ToString();
  const std::vector<ColumnSchema>& columns = schema.columns();

  size_t i = 0;
  for (const auto& col : columns) {
    // 'ybrowid' is a column present only in DocDB. Skip it.
    if (col.name() == "ybrowid") {
      continue;
    }

    if (i >= pg_cols.size()) {
      LOG(INFO) << fail_msg << Format(" Expected num_columns: $0 num_columns in PG: $1",
          columns.size(), pg_cols.size());
      return false;
    }

    if (col.name().compare(pg_cols[i].attname) != 0) {
      LOG(INFO) << fail_msg << Format(" Expected column name with attnum: $0 is :$1"
          " but column name at PG is $2", pg_cols[i].order, col.name(), pg_cols[i].attname);
      return false;
    }

    // Verify whether attnum matches.
    if (col.order() != pg_cols[i].order) {
      LOG(INFO) << fail_msg << Format(" At index $0 expected attnum is $1 but actual attnum is $2",
          i, col.order(), pg_cols[i].order);
      return false;
    }
    i++;
  }

  return true;
}

Result<vector<YsqlTransactionDdl::PgColumnFields>>
YsqlTransactionDdl::ReadPgAttribute(scoped_refptr<TableInfo> table) {
  // Build schema using values read from pg_attribute.
  auto tablet_peer = sys_catalog_->tablet_peer();
  const tablet::TabletPtr tablet = tablet_peer->shared_tablet();

  const uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTableId(table->id()));
  const uint32_t table_oid = VERIFY_RESULT(GetPgsqlTableOid(table->id()));
  const auto& pg_attribute_table_id = GetPgsqlTableId(database_oid, kPgAttributeTableOid);

  Schema projection;
  uint32_t oid = VERIFY_RESULT(GetPgsqlTableOid(table->id()));
  auto iter = VERIFY_RESULT(GetPgCatalogTableScanIterator(
      pg_attribute_table_id,
      "attrelid" /* col_name */,
      oid,
      {"attrelid", "attnum", "attname", "atttypid"},
      &projection));

  const auto attname_col_id = VERIFY_RESULT(projection.ColumnIdByName("attname")).rep();
  const auto atttypid_col_id = VERIFY_RESULT(projection.ColumnIdByName("atttypid")).rep();
  const auto attnum_col_id = VERIFY_RESULT(projection.ColumnIdByName("attnum")).rep();

  vector<PgColumnFields> pg_cols;
  while (VERIFY_RESULT(iter->HasNext())) {
    QLTableRow row;
    RETURN_NOT_OK(iter->NextRow(&row));

    const auto& attname_col = row.GetValue(attname_col_id);
    const auto& atttypid_col = row.GetValue(atttypid_col_id);
    const auto& attnum_col = row.GetValue(attnum_col_id);
    if (!attname_col || !atttypid_col || !attnum_col) {
      std::string corrupted_col =
        !attname_col ? "attname" : !atttypid_col ? "atttypid" : "attnum";
      return STATUS_FORMAT(
          Corruption,
          "Could not read $0 column from pg_attribute for attrelid: $1 database_oid: $2",
          corrupted_col, table_oid, database_oid);
    }

    const int32_t attnum = attnum_col->int16_value();
    if (attnum < 0) {
      // Ignore system columns.
      VLOG(3) << "Ignoring system column (attnum = " << attnum_col->int16_value()
              << ") for attrelid:" << table_oid;
      continue;
    }
    string attname = attname_col->string_value();
    uint32_t atttypid = atttypid_col->uint32_value();
    if (atttypid == 0) {
      // Ignore dropped columns.
      VLOG(3) << "Ignoring dropped column " << attname << " (atttypid = 0)"
              << " for attrelid:" << table_oid;
      continue;
    }
    VLOG(3) << "attrelid: " << table_oid << " attname: " << attname << " atttypid: " << atttypid;
    pg_cols.emplace_back(attnum, attname);
  }

  return pg_cols;
}

}  // namespace master
}  // namespace yb
