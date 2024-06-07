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
#include <sstream>

#include "yb/master/ysql_ddl_verification_task.h"

#include "yb/client/transaction_rpc.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/common/colocated_util.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/status_log.h"

#include "yb/util/flags/flag_tags.h"

DEFINE_UNKNOWN_int32(ysql_transaction_bg_task_wait_ms, 200,
  "Amount of time the catalog manager background task thread waits "
  "between runs");

DEFINE_test_flag(bool, skip_transaction_verification, false,
    "Test only flag to keep the txn metadata in SysTablesEntryPB and skip"
    " transaction verification on the master");

DEFINE_test_flag(int32, ysql_ddl_transaction_verification_failure_percentage, 0,
    "Inject random failure in checking transaction status for DDL transactions");

DEFINE_test_flag(bool, yb_test_table_rewrite_keep_old_table, false,
    "Used together with the PG GUC yb_test_table_rewrite_keep_old_table in "
    "some unit tests where we want to test schema version mismatch error on "
    "concurrent DMLs. If the table is dropped too soon, we will just get a "
    "table does not exist error instead.");

using std::string;
using std::vector;

namespace yb {
namespace master {

static const char* kTableNameColName = "relname";
static const char* kTablegroupNameColName = "grpname";

namespace {

struct PgColumnFields {
  // Order determines the order in which the columns were created. This is equal to the
  // 'attnum' field in the pg_attribute table in PG catalog.
  int order;
  std::string attname;

  PgColumnFields(int attnum, std::string name) : order(attnum), attname(name) {}
};

string PrintPgCols(const vector<PgColumnFields>& pg_cols) {
  std::stringstream ss;
  ss << "{ ";
  for (const auto& col : pg_cols) {
    ss << "{" << col.attname << ", " << col.order << "} ";
  }
  ss << "}";
  return ss.str();
}

Status PgEntryExistsWithReadTime(
    SysCatalogTable* sys_catalog,
    const TableId& pg_table_id,
    PgOid entry_oid,
    boost::optional<PgOid> relfilenode_oid,
    const ReadHybridTime& read_time,
    bool* result,
    HybridTime* read_restart_ht);

Status PgSchemaCheckerWithReadTime(SysCatalogTable* sys_catalog,
                                   const scoped_refptr<TableInfo>& table,
                                   const ReadHybridTime& read_time,
                                   bool* result,
                                   HybridTime* read_restart_ht);

bool MatchPgDocDBSchemaColumns(const scoped_refptr<TableInfo>& table,
                               const Schema& schema,
                               const vector<PgColumnFields>& pg_cols);

Result<vector<PgColumnFields>> ReadPgAttribute(SysCatalogTable* sys_catalog,
                                               scoped_refptr<TableInfo> table);

Status ReadPgAttributeWithReadTime(
    SysCatalogTable* sys_catalog,
    const scoped_refptr<TableInfo>& table,
    const ReadHybridTime& read_time,
    vector<PgColumnFields>* pg_cols,
    HybridTime* read_restart_ht);

Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>>
GetPgCatalogTableScanIterator(
  SysCatalogTable* sys_catalog,
  const PgTableReadData& read_data,
  PgOid oid_value,
  const dockv::ReaderProjection& projection,
  RequestScope* request_scope) {
  // Use Scan to query the given table, filtering by lookup_oid_col.
  auto iter = VERIFY_RESULT(read_data.NewUninitializedIterator(projection));

  PgsqlConditionPB cond;
  cond.add_operands()->set_column_id(projection.columns.front().id.rep());
  cond.set_op(QL_OP_EQUAL);
  cond.add_operands()->mutable_value()->set_uint32_value(oid_value);
  const dockv::KeyEntryValues empty_key_components;
  docdb::DocPgsqlScanSpec spec(
      read_data.schema(), rocksdb::kDefaultQueryId, empty_key_components, empty_key_components,
      &cond, std::nullopt /* hash_code */, std::nullopt /* max_hash_code */);
  // Grab a RequestScope to prevent intent clean up, before we Init the iterator.
  *request_scope = VERIFY_RESULT(VERIFY_RESULT(sys_catalog->Tablet())->CreateRequestScope());
  RETURN_NOT_OK(iter->Init(spec));
  return iter;
}

Result<bool> PgEntryExists(SysCatalogTable& sys_catalog,
                           const TableId& pg_table_id,
                           PgOid entry_oid,
                           boost::optional<PgOid> relfilenode_oid) {
  bool result = false;
  RETURN_NOT_OK(sys_catalog.ReadWithRestarts(std::bind(
      &PgEntryExistsWithReadTime, &sys_catalog, pg_table_id, entry_oid, relfilenode_oid,
      std::placeholders::_1, &result, std::placeholders::_2)));
  return result;
}

// Note: "relfilenode_oid" is only used for rewritten tables. For rewritten tables, we need to
// check both the oid and the relfilenode columns in pg_class.
Status PgEntryExistsWithReadTime(
    SysCatalogTable* sys_catalog,
    const TableId& pg_table_id,
    PgOid entry_oid,
    boost::optional<PgOid> relfilenode_oid,
    const ReadHybridTime& read_time,
    bool* result,
    HybridTime* read_restart_ht) {
  auto read_data = VERIFY_RESULT(sys_catalog->TableReadData(pg_table_id, read_time));

  auto oid_col = VERIFY_RESULT(read_data.ColumnByName("oid")).rep();
  ColumnIdRep relfilenode_col = kInvalidColumnId.rep();

  dockv::ReaderProjection projection;

  bool is_rewritten_table = relfilenode_oid.has_value();
  if (is_rewritten_table) {
    relfilenode_col = VERIFY_RESULT(read_data.ColumnByName("relfilenode"));
    projection.Init(read_data.schema(), {oid_col, relfilenode_col});
  } else {
    projection.Init(read_data.schema(), {oid_col});
  }

  RequestScope request_scope;
  auto iter = VERIFY_RESULT(
      GetPgCatalogTableScanIterator(sys_catalog, read_data, entry_oid, projection, &request_scope));

  // If no rows found, the entry does not exist.
  qlexpr::QLTableRow row;
  if (!VERIFY_RESULT(iter->FetchNext(&row))) {
    *read_restart_ht = VERIFY_RESULT(iter->RestartReadHt());
    *result = false;
    return Status::OK();
  }

  // The entry exists. Expect only one row.
  SCHECK(!VERIFY_RESULT(iter->FetchNext(nullptr)), Corruption, "Too many rows found");
  if (is_rewritten_table) {
    const auto& relfilenode = row.GetValue(relfilenode_col);
    if (relfilenode->uint32_value() != *relfilenode_oid) {
      *read_restart_ht = VERIFY_RESULT(iter->RestartReadHt());
      *result = false;
      return Status::OK();
    }
  }
  *read_restart_ht = VERIFY_RESULT(iter->RestartReadHt());
  *result = true;
  return Status::OK();
}

Result<bool> PgSchemaChecker(SysCatalogTable& sys_catalog, const scoped_refptr<TableInfo>& table) {
  bool result = false;
  RETURN_NOT_OK(sys_catalog.ReadWithRestarts(
    std::bind(&PgSchemaCheckerWithReadTime, &sys_catalog, table, std::placeholders::_1, &result,
      std::placeholders::_2)));
  return result;
}

Status PgSchemaCheckerWithReadTime(SysCatalogTable* sys_catalog,
                                   const scoped_refptr<TableInfo>& table,
                                   const ReadHybridTime& read_time,
                                   bool* result,
                                   HybridTime* read_restart_ht) {
  PgOid oid = kPgInvalidOid;
  string pg_catalog_table_id, name_col;

  if (table->IsColocationParentTable()) {
    // The table we have is a dummy parent table, hence not present in YSQL.
    // We need to check a tablegroup instead.
    const auto tablegroup_id = GetTablegroupIdFromParentTableId(table->id());
    const PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTablegroupId(tablegroup_id));
    const auto pg_yb_tablegroup_table_id = GetPgsqlTableId(database_oid, kPgYbTablegroupTableOid);
    oid = VERIFY_RESULT(GetPgsqlTablegroupOid(tablegroup_id));
    pg_catalog_table_id = GetPgsqlTableId(database_oid, kPgYbTablegroupTableOid);
    name_col = kTablegroupNameColName;
  } else {
    const PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTableId(table->id()));
    pg_catalog_table_id = GetPgsqlTableId(database_oid, kPgClassTableOid);
    oid = VERIFY_RESULT(table->GetPgTableOid());
    name_col = kTableNameColName;
  }

  auto read_data = VERIFY_RESULT(sys_catalog->TableReadData(pg_catalog_table_id, read_time));
  auto oid_col_id = VERIFY_RESULT(read_data.ColumnByName("oid")).rep();
  auto relname_col_id = VERIFY_RESULT(read_data.ColumnByName(name_col)).rep();
  auto relkind_col_id = VERIFY_RESULT(read_data.ColumnByName("relkind")).rep();
  dockv::ReaderProjection projection;

  ColumnIdRep relfilenode_col_id = kInvalidColumnId.rep();
  // If this isn't a system table, we need to check if the relfilenode in pg_class matches the
  // DocDB table ID (as the table may have been rewritten).
  // For colocated parent table are never rewritten so we can skip them. Also, they do not exist in
  // YSQL so we cannot check its relfilenode.
  bool check_relfilenode = !table->is_system() && !table->IsColocationParentTable();
  if (check_relfilenode) {
    relfilenode_col_id = VERIFY_RESULT(read_data.ColumnByName("relfilenode")).rep();
    projection.Init(read_data.schema(),
        {oid_col_id, relname_col_id, relfilenode_col_id, relkind_col_id});
  } else {
    projection.Init(read_data.schema(), {oid_col_id, relname_col_id});
  }

  RequestScope request_scope;
  auto iter = VERIFY_RESULT(GetPgCatalogTableScanIterator(
      sys_catalog, read_data, oid, projection, &request_scope));

  auto l = table->LockForRead();
  if (!l->has_ysql_ddl_txn_verifier_state()) {
    // The table no longer has transaction verifier state on it, it was probably cleaned up
    // concurrently.
    return STATUS_FORMAT(Aborted, "Not performing transaction verification for table $0 as it no "
                         "longer has any transaction verification state", table->ToString());
  }

  qlexpr::QLTableRow row;
  bool table_found = false;
  bool table_rewritten = false;

  if (VERIFY_RESULT(iter->FetchNext(&row))) {
    // One row found in pg_class matching the oid. Perform the check on the relfilenode column, if
    // required (as in the case of table rewrite).
    table_found = true;
    // Table rewrites don't affect parent partition tables (only their children),
    // so we can skip relfilenode checks for them.
    if (check_relfilenode && row.GetValue(relkind_col_id)->int8_value() != int8_t('p')) {
      const auto& relfilenode_col = row.GetValue(relfilenode_col_id);
      if (relfilenode_col->uint32_value() != VERIFY_RESULT(table->GetPgRelfilenodeOid())) {
        table_found = false;
        table_rewritten = true;
      }
    }
  }

  // Table not found in pg_class. This can only happen in three cases:
  // * Table creation failed,
  // * A table deletion went through successfully,
  // * In some unit tests where --TEST_yb_test_table_rewrite_keep_old_table=true
  //   is set on yb-master and PG GUC yb_test_table_rewrite_keep_old_table is true,
  //   an ALTER TABLE statement that involves a table rewrite will not have
  //   "contains_drop_table_op" set, therefore is_being_deleted_by_ysql_ddl_txn()
  //   is false. If such an ALTER TABLE statement went through successfully, then
  //   the old PG table is deleted from pg_class while the old DocDB table is kept
  //   for testing purpose.
  if (!table_found) {
    *read_restart_ht = VERIFY_RESULT(iter->RestartReadHt());
    if (l->is_being_deleted_by_ysql_ddl_txn()) {
      *result = true;
      return Status::OK();
    }
    if (FLAGS_TEST_yb_test_table_rewrite_keep_old_table) {
      // If alter table resulted in a table rewrite and the old table is
      // already deleted from PG catalog after the ddl transaction completes,
      // then the ddl transaction must have committed because the old table
      // should still exist if the ddl transaction aborted.
      if (l->is_being_altered_by_ysql_ddl_txn() && table_rewritten) {
        *result = true;
        return Status::OK();
      }
    }
    CHECK(l->is_being_created_by_ysql_ddl_txn())
        << table->ToString() << " " << l->pb.ysql_ddl_txn_verifier_state(0).ShortDebugString();
    *result = false;
    return Status::OK();
  }

  // Table present in PG catalog.
  *read_restart_ht = VERIFY_RESULT(iter->RestartReadHt());

  if (l->is_being_deleted_by_ysql_ddl_txn()) {
    LOG(INFO) << "Ysql Drop transaction for " << table->ToString()
              << " detected to have failed as table found in PG catalog";
    *result = false;
    return Status::OK();
  }

  if (l->is_being_created_by_ysql_ddl_txn()) {
    *result = true;
    return Status::OK();
  }

  // Table was being altered. Check whether its current DocDB schema matches
  // that of PG catalog.
  CHECK(l->ysql_ddl_txn_verifier_state().contains_alter_table_op());
  const auto& relname_col = row.GetValue(relname_col_id);
  const string& table_name = relname_col->string_value();

  const string fail_msg = "Alter transaction on " + table->ToString() + " failed.";
  if (table->name().compare(table_name) != 0) {
    // Table name does not match.
    LOG(INFO) << fail_msg << " Expected table name: " << table->name() << " Table name in PG: "
              << table_name;
    CHECK_EQ(table_name, l->ysql_ddl_txn_verifier_state().previous_table_name());
    *result = false;
    return Status::OK();
  }

  vector<PgColumnFields> pg_cols = VERIFY_RESULT(ReadPgAttribute(sys_catalog, table));
  // In DocDB schema, columns are sorted based on 'order'.
  sort(pg_cols.begin(), pg_cols.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.order < rhs.order;
  });

  Schema schema;
  RETURN_NOT_OK(table->GetSchema(&schema));
  if (MatchPgDocDBSchemaColumns(table, schema, pg_cols)) {
    // The PG catalog schema matches the current DocDB schema. The transaction was a success.
    *result = true;
    return Status::OK();
  }

  Schema previous_schema;
  RETURN_NOT_OK(SchemaFromPB(l->ysql_ddl_txn_verifier_state().previous_schema(), &previous_schema));
  if (MatchPgDocDBSchemaColumns(table, previous_schema, pg_cols)) {
    // The PG catalog schema matches the DocDB schema of the table prior to this transaction. The
    // transaction must have aborted.
    *result = false;
    return Status::OK();
  }

  // The PG catalog schema does not match either the current schema nor the previous schema. This
  // is an unexpected state, do nothing.
  LOG(WARNING) << "Unexpected state for table " << table->ToString() << " with current schema "
               << schema.ToString() << " and previous schema " << previous_schema.ToString()
               << " and PG catalog schema " << PrintPgCols(pg_cols)
               << ". The transaction verification state is "
               << l->ysql_ddl_txn_verifier_state().ShortDebugString();
  return STATUS_FORMAT(Corruption, "Failed to verify DDL transaction for table $0",
                       table->ToString());
}

bool MatchPgDocDBSchemaColumns(
  const scoped_refptr<TableInfo>& table,
  const Schema& schema,
  const vector<PgColumnFields>& pg_cols) {

  const string& fail_msg = "Schema mismatch for table " + table->ToString() + " with schema "
                          + schema.ToString() + " and PG catalog schema " + PrintPgCols(pg_cols);
  std::vector<ColumnSchema> columns = schema.columns();
  // Sort the columns based on the "order" field. This corresponds to PG attnum field. The pg
  // columns will also be sorted on attnum, so the comparison will be easier.
  sort(columns.begin(), columns.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.order() < rhs.order();
  });

  size_t pg_idx = 0;
  for (const auto& col : columns) {
    // 'ybrowid' is a column present only in DocDB. Skip it.
    if (col.name() == "ybrowid" || col.name() == "ybidxbasectid" ||
        col.name() == "ybuniqueidxkeysuffix") {
      continue;
    }

    if (col.marked_for_deletion() && pg_idx < pg_cols.size() &&
        col.order() == pg_cols[pg_idx].order) {
      LOG(INFO) << fail_msg << " Column " << col.name() << " is marked for deletion but found it "
          "in PG catalog";
      return false;
    }

    // The column is marked for deletion on DocDB side. We didn't find it in the PG catalog as
    // expected, so safely skip this column.
    if (col.marked_for_deletion()) {
      continue;
    }

    if (pg_idx >= pg_cols.size()) {
      LOG(INFO) << fail_msg << " Expected num_columns: " << columns.size()
                << " but found num_columns in PG: " << pg_cols.size();
      return false;
    }

    if (col.name().compare(pg_cols[pg_idx].attname) != 0) {
      LOG(INFO) << fail_msg << " Expected column name for attnum: " << pg_cols[pg_idx].order
                << " is :" << col.name() << " but column name at PG is " << pg_cols[pg_idx].attname;
      return false;
    }

    // Verify whether attnum matches.
    if (col.order() != pg_cols[pg_idx].order) {
      LOG(INFO) << fail_msg << " At index " << pg_idx << " expected attnum is " << col.order()
                << " but actual attnum is " << pg_cols[pg_idx].order;
      return false;
    }
    pg_idx++;
  }

  return true;
}

Result<vector<PgColumnFields>> ReadPgAttribute(SysCatalogTable* sys_catalog,
                                               scoped_refptr<TableInfo> table) {
  vector<PgColumnFields> pg_cols;
  RETURN_NOT_OK(sys_catalog->ReadWithRestarts(std::bind(
      &ReadPgAttributeWithReadTime, sys_catalog, table, std::placeholders::_1,
      &pg_cols, std::placeholders::_2)));
  return pg_cols;
}

Status ReadPgAttributeWithReadTime(
    SysCatalogTable* sys_catalog,
    const scoped_refptr<TableInfo>& table,
    const ReadHybridTime& read_time,
    vector<PgColumnFields>* pg_cols,
    HybridTime* read_restart_ht) {
  // Build schema using values read from pg_attribute.

  const PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTableId(table->id()));
  const PgOid table_oid = VERIFY_RESULT(table->GetPgTableOid());
  auto read_data =
      VERIFY_RESULT(sys_catalog->TableReadData(database_oid, kPgAttributeTableOid, read_time));
  const auto attrelid_col_id = VERIFY_RESULT(read_data.ColumnByName("attrelid")).rep();
  const auto attname_col_id = VERIFY_RESULT(read_data.ColumnByName("attname")).rep();
  const auto atttypid_col_id = VERIFY_RESULT(read_data.ColumnByName("atttypid")).rep();
  const auto attnum_col_id = VERIFY_RESULT(read_data.ColumnByName("attnum")).rep();

  dockv::ReaderProjection projection(
      read_data.schema(), { attrelid_col_id, attnum_col_id, attname_col_id, atttypid_col_id });
  RequestScope request_scope;
  auto iter =
      VERIFY_RESULT(GetPgCatalogTableScanIterator(
          sys_catalog, read_data, table_oid, projection, &request_scope));

  qlexpr::QLTableRow row;
  while (VERIFY_RESULT(iter->FetchNext(&row))) {
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
    PgOid atttypid = atttypid_col->uint32_value();
    if (atttypid == 0) {
      // Ignore dropped columns.
      VLOG(3) << "Ignoring dropped column " << attname << " (atttypid = 0)"
              << " for attrelid:" << table_oid;
      continue;
    }
    VLOG(3) << "attrelid: " << table_oid << " attname: " << attname << " atttypid: " << atttypid;
    pg_cols->emplace_back(attnum, attname);
  }

  *read_restart_ht = VERIFY_RESULT(iter->RestartReadHt());
  return Status::OK();
}
} // namespace

PollTransactionStatusBase::PollTransactionStatusBase(
    const TransactionMetadata& transaction, std::shared_future<client::YBClient*> client_future)
    : transaction_(transaction), client_future_(std::move(client_future)) {
  sync_.StatusCB(Status::OK());
}

PollTransactionStatusBase::~PollTransactionStatusBase() {
  std::lock_guard l(rpc_mutex_);
  DCHECK(shutdown_) << "Shutdown not invoked";
}

void PollTransactionStatusBase::Shutdown() {
  std::lock_guard l(rpc_mutex_);

  rpcs_.Shutdown();
  // Shutdown times out after waiting a certain amount of time. The callback requires both this and
  // the Rpcs to be valid, so wait for it to complete.
  CHECK_OK(sync_.Wait());

  shutdown_ = true;
}

Status PollTransactionStatusBase::VerifyTransaction() {
  if (FLAGS_TEST_skip_transaction_verification) {
    return Status::OK();
  }

  YB_LOG_EVERY_N_SECS(INFO, 1) << "Verifying Transaction " << transaction_;

  tserver::GetTransactionStatusRequestPB req;
  req.set_tablet_id(transaction_.status_tablet);
  req.add_transaction_id()->assign(
      pointer_cast<const char*>(transaction_.transaction_id.data()),
      transaction_.transaction_id.size());

  auto client = client_future_.get();
  if (!client) {
    return STATUS_FORMAT(IllegalState,
        "Shutting down. Cannot send GetTransactionStatus: $0", transaction_);
  }

  RETURN_NOT_OK(sync_.Wait());
  sync_.Reset();

  std::lock_guard l(rpc_mutex_);
  SCHECK(
      !shutdown_, IllegalState, "Task has been shut down. Cannot send GetTransactionStatus: $0",
      transaction_);

  // Prepare the rpc after checking if it is shutting down in case it returns because of
  // client is null and leave the reserved rpc as uninitialized.
  auto rpc_handle = rpcs_.Prepare();
  if (rpc_handle == rpcs_.InvalidHandle()) {
    return STATUS_FORMAT(IllegalState,
        "Shutting down. Cannot send GetTransactionStatus: $0", transaction_);
  }
  // We need to query the TransactionCoordinator here.  Can't use TransactionStatusResolver in
  // TransactionParticipant since this TransactionMetadata may not have any actual data flushed yet.
  auto sync_cb = sync_.AsStdStatusCallback();
  auto callback = [this, rpc_handle, user_cb = std::move(sync_cb)](
                      Status status, const tserver::GetTransactionStatusResponsePB& resp) {
    auto retained = rpcs_.Unregister(rpc_handle);
    TransactionReceived(std::move(status), resp);
    user_cb(Status::OK());
  };
  *rpc_handle = client::GetTransactionStatus(
      TransactionRpcDeadline(), nullptr /* tablet */, client, &req, std::move(callback));
  (**rpc_handle).SendRpc();
  return Status::OK();
}

void PollTransactionStatusBase::TransactionReceived(
    Status txn_status, const tserver::GetTransactionStatusResponsePB& resp) {
  if (FLAGS_TEST_ysql_ddl_transaction_verification_failure_percentage > 0 &&
    RandomUniformInt(1, 99) <= FLAGS_TEST_ysql_ddl_transaction_verification_failure_percentage) {
    LOG(ERROR) << "Injecting failure for transaction, inducing failure to enqueue callback";
    FinishPollTransaction(STATUS_FORMAT(InternalError, "Injected failure"));
    return;
  }

  if (!txn_status.ok()) {
    LOG(WARNING) << "Transaction Status attempt (" << transaction_
                 << ") failed with status " << txn_status;
    FinishPollTransaction(txn_status);
    return;
  }
  if (resp.has_error()) {
    const Status s = StatusFromPB(resp.error().status());
    FinishPollTransaction(STATUS_FORMAT(
        InternalError, "Transaction Status attempt failed with error code $0: $1",
        tserver::TabletServerErrorPB::Code_Name(resp.error().code()), s));
    return;
  }
  YB_LOG_EVERY_N_SECS(INFO, 1) << "Got Response for " << transaction_
                               << ", resp: " << resp.ShortDebugString();
  bool is_pending = (resp.status_size() == 0);
  for (int i = 0; i < resp.status_size() && !is_pending; ++i) {
    // NOTE: COMMITTED state is also "pending" because we need APPLIED.
    is_pending = resp.status(i) == TransactionStatus::PENDING ||
                 resp.status(i) == TransactionStatus::COMMITTED;
  }
  if (is_pending) {
    TransactionPending();
    return;
  }
  // If this transaction isn't pending, then the transaction is in a terminal state.
  // Note: We ignore the resp.status() now, because it could be ABORT'd but actually a SUCCESS.
  // Determine whether the transaction was a success by comparing with the PG schema.
  FinishPollTransaction(Status::OK());
}

NamespaceVerificationTask::NamespaceVerificationTask(
    CatalogManager& catalog_manager, scoped_refptr<NamespaceInfo> ns,
    const TransactionMetadata& transaction, std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog, std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger, const LeaderEpoch& epoch)
    : MultiStepNamespaceTaskBase(
          catalog_manager, *catalog_manager.AsyncTaskPool(), messenger, *ns, epoch),
      PollTransactionStatusBase(transaction, std::move(client_future)),
      sys_catalog_(*sys_catalog) {
  completion_callback_ = [this,
                          complete_callback = std::move(complete_callback)](const Status& status) {
    if (!status.ok()) {
      complete_callback(status);
    } else {
      complete_callback(entry_exists_);
    }
  };
}

void NamespaceVerificationTask::CreateAndStartTask(
    CatalogManager& catalog_manager,
    scoped_refptr<NamespaceInfo> ns,
    const TransactionMetadata& transaction,
    std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog,
    std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger,
    const LeaderEpoch& epoch) {

  auto task = std::make_shared<NamespaceVerificationTask>(catalog_manager, ns, transaction,
                                                          std::move(complete_callback),
                                                          sys_catalog,
                                                          std::move(client_future),
                                                          messenger, epoch);

  task->Start();
}

Status NamespaceVerificationTask::FirstStep() {
  // Schedule verify transaction with some delay.
  this->ScheduleNextStepWithDelay([this] {
    return VerifyTransaction();
  }, "VerifyTransaction", MonoDelta::FromMilliseconds(FLAGS_ysql_transaction_bg_task_wait_ms));
  return Status::OK();
}

void NamespaceVerificationTask::TransactionPending() {
  this->ScheduleNextStep([this] {
    return VerifyTransaction();
  }, "VerifyTransaction");
}

void NamespaceVerificationTask::FinishPollTransaction(Status status) {
  ScheduleNextStep(
    std::bind(&NamespaceVerificationTask::CheckNsExists, this, std::move(status)),
    "CheckNsExists");
}

Status NamespaceVerificationTask::CheckNsExists(Status txn_rpc_status) {
  RETURN_NOT_OK(txn_rpc_status);
  const auto pg_table_id = GetPgsqlTableId(atoi(kSystemNamespaceId), kPgDatabaseTableOid);
  const PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_info_.id()));
  entry_exists_ = VERIFY_RESULT(
      PgEntryExists(sys_catalog_, pg_table_id, database_oid, boost::none /* relfilenodeoid */));

  Complete();
  return Status::OK();
}

Status NamespaceVerificationTask::ValidateRunnable() {
  RETURN_NOT_OK(MultiStepNamespaceTaskBase::ValidateRunnable());
  const auto& l = namespace_info_.LockForRead();
  SCHECK(l->pb.has_transaction(), IllegalState,
         "Namespace $0 is not being modified by any transaction", namespace_info_.ToString());
  SCHECK_EQ(l->pb.state(), SysNamespaceEntryPB::RUNNING, Aborted,
            Format("Invalid Namespace state ($0), abandoning transaction GC work for $1",
                   SysNamespaceEntryPB_State_Name(l->pb.state()), namespace_info_.ToString()));
  return Status::OK();
}

void NamespaceVerificationTask::TaskCompleted(const Status& status) {
  MultiStepNamespaceTaskBase::TaskCompleted(status);
  Shutdown();
}

void NamespaceVerificationTask::PerformAbort() {
  MultiStepNamespaceTaskBase::PerformAbort();
  Shutdown();
}

TableSchemaVerificationTask::TableSchemaVerificationTask(
    CatalogManager& catalog_manager, scoped_refptr<TableInfo> table,
    const TransactionMetadata& transaction, std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog, std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger, const LeaderEpoch& epoch, bool ddl_atomicity_enabled)
    : MultiStepTableTaskBase(
          catalog_manager, *catalog_manager.AsyncTaskPool(), messenger, std::move(table), epoch),
      PollTransactionStatusBase(transaction, std::move(client_future)),
      sys_catalog_(*sys_catalog),
      ddl_atomicity_enabled_(ddl_atomicity_enabled) {
  completion_callback_ = [this,
                          complete_callback = std::move(complete_callback)](const Status& status) {
    if (!status.ok()) {
      complete_callback(status);
    } else {
      complete_callback(is_committed_);
    }
  };
}

void TableSchemaVerificationTask::CreateAndStartTask(
    CatalogManager& catalog_manager,
    scoped_refptr<TableInfo> table,
    const TransactionMetadata& transaction,
    std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog,
    std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger,
    const LeaderEpoch& epoch,
    bool ddl_atomicity_enabled) {

  auto task = std::make_shared<TableSchemaVerificationTask>(
      catalog_manager, table, transaction, complete_callback, sys_catalog,
      std::move(client_future), messenger, epoch, ddl_atomicity_enabled);

  task->Start();
}

Status TableSchemaVerificationTask::FirstStep() {
  // Schedule verify transaction with some delay.
  this->ScheduleNextStepWithDelay([this] {
    return VerifyTransaction();
  }, "VerifyTransaction", MonoDelta::FromMilliseconds(FLAGS_ysql_transaction_bg_task_wait_ms));
  return Status::OK();
}

void TableSchemaVerificationTask::TransactionPending() {
  // Schedule verify transaction with some delay.
  this->ScheduleNextStep([this] {
    return VerifyTransaction();
  }, "VerifyTransaction");
}

Status TableSchemaVerificationTask::ValidateRunnable() {
  RETURN_NOT_OK(MultiStepTableTaskBase::ValidateRunnable());
  if (!ddl_atomicity_enabled_) {
    return Status::OK();
  }
  auto l = table_info_->LockForRead();
  SCHECK(VERIFY_RESULT(l->is_being_modified_by_ddl_transaction(transaction_.transaction_id)),
         IllegalState,
         "Table $0 is being modified by transaction $1, not $2", table_info_->ToString(),
             l->GetCurrentDdlTransactionId(), transaction_.transaction_id);
  SCHECK(table_info_->is_running(), IllegalState, "Task $0 failed since table is in state $1",
      description(), l->state_name());
  return Status::OK();
}

void TableSchemaVerificationTask::FinishPollTransaction(Status status) {
  ScheduleNextStep([this, status] {
    return ddl_atomicity_enabled_ ? CompareSchema(status) : CheckTableExists(status);
  }, "Compare Schema");
}

Status TableSchemaVerificationTask::FinishTask(Result<bool> is_committed) {
  RETURN_NOT_OK(is_committed);

  is_committed_ = *is_committed;
  Complete();
  return Status::OK();
}

Status TableSchemaVerificationTask::CheckTableExists(Status txn_rpc_success) {
  RETURN_NOT_OK(txn_rpc_success);
  if (!table_info_->IsColocationParentTable()) {
    // Check that pg_class still has an entry for the table.
    const PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTableId(table_info_->id()));
    const auto pg_class_table_id = GetPgsqlTableId(database_oid, kPgClassTableOid);

    PgOid pg_table_oid = VERIFY_RESULT(table_info_->GetPgTableOid());
    PgOid relfilenode_oid = VERIFY_RESULT(table_info_->GetPgRelfilenodeOid());

    return FinishTask(PgEntryExists(
        sys_catalog_, pg_class_table_id, pg_table_oid,
        // If relfilenode_oid is the same as pg table oid, this is isn't a rewritten table and
        // we don't need to perform additional checks on the relfilenode column.
        relfilenode_oid == pg_table_oid ? boost::none : boost::make_optional(relfilenode_oid)));
  }
  // The table we have is a dummy parent table, hence not present in YSQL.
  // We need to check a tablegroup instead.
  const auto tablegroup_id = GetTablegroupIdFromParentTableId(table_info_->id());
  const PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOidByTablegroupId(tablegroup_id));
  const auto pg_yb_tablegroup_table_id = GetPgsqlTableId(database_oid, kPgYbTablegroupTableOid);
  const PgOid tablegroup_oid = VERIFY_RESULT(GetPgsqlTablegroupOid(tablegroup_id));

  return FinishTask(PgEntryExists(
      sys_catalog_, pg_yb_tablegroup_table_id, tablegroup_oid, boost::none /* relfilenode_oid */));
}

Status TableSchemaVerificationTask::CompareSchema(Status txn_rpc_success) {
  RETURN_NOT_OK(txn_rpc_success);

  // If the transaction was a success, we need to compare the schema of the table in PG catalog
  // with the schema in DocDB.
  return FinishTask(PgSchemaChecker(sys_catalog_, table_info_));
}

void TableSchemaVerificationTask::TaskCompleted(const Status& status) {
  MultiStepTableTaskBase::TaskCompleted(status);
  Shutdown();
}

void TableSchemaVerificationTask::PerformAbort() {
  MultiStepTableTaskBase::PerformAbort();
  Shutdown();
}

}  // namespace master
}  // namespace yb
