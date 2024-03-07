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

#include "yb/client/client_fwd.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-test-util.h"

#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"

#include "yb/master/master_client.pb.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/timestamp.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_ddl_atomicity_test_base.h"

namespace yb::pgwrapper {

// Setup functions.
vector<string> PgDdlAtomicityTestBase::GetAllDDLs() {
  static const auto ddls =
     { CreateTableStmt(kCreateTable), RenameTableStmt(kRenameTable),
       RenameColumnStmt(kRenameCol), AddColumnStmt(kAddCol), DropColumnStmt(kDropCol),
       DropTableStmt(kDropTable), CreateIndexStmt(kCreateIndex, kIndexedTable),
       RenameIndexStmt(kRenameIndex, kRenamedIndex), DropIndexStmt(kDropIndex),
       AddPkStmt(kAddPk), DropPkStmt(kDropPk) };
  return ddls;
}

Status PgDdlAtomicityTestBase::SetupTablesForAllDdls(PGConn *conn,
                                                     const int num_rows_to_insert,
                                                     const string& tablegroup_name) {
  const auto tables = {kRenameTable, kRenameCol, kAddCol, kDropCol, kDropTable,
                       kIndexedTable, kAddPk, kDropPk};
  const auto indexes = {kRenameIndex, kDropIndex};
  return SetupTables(conn, tables, indexes, num_rows_to_insert, tablegroup_name);
}

Status PgDdlAtomicityTestBase::SetupTables(PGConn *conn,
                                           const vector<TableName>& tables,
                                           const vector<TableName>& indexes,
                                           const int num_rows_to_insert,
                                           const string& tablegroup_name) {
  for (const auto& table : tables) {
    RETURN_NOT_OK(conn->Execute(table == kAddPk ?
        CreateTableWithoutPk(table, tablegroup_name) : CreateTableStmt(table, tablegroup_name)));

    for (int i = 0; i < num_rows_to_insert; ++i) {
      RETURN_NOT_OK(
          conn->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'value$1', 1.1)", table, i + 5));
    }
  }
  for (const auto& index : indexes) {
    RETURN_NOT_OK(conn->Execute(CreateIndexStmt(index, kIndexedTable)));
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::RunAllDdls(PGConn* conn) {
  for (const auto& ddl : GetAllDDLs()) {
    RETURN_NOT_OK(conn->Execute(ddl));
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::RunAllDdlsWithErrorInjection(PGConn* conn) {
  for (const auto& ddl : GetAllDDLs()) {
    RETURN_NOT_OK(conn->TestFailDdl(ddl));
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::VerifyAllSuccessfulDdls(PGConn *conn,
                                                       client::YBClient* client,
                                                       const string& database) {
  // Verify that all the DDLs that were run as part of 'RunAllDdls' are successful and are in the
  // expected state.
  // After successful create, we must find the tables.
  VerifyTableExists(client, database, kCreateTable, 10);
  VerifyTableExists(client, database, kCreateIndex, 10);

  // After successful rename, the tables should be present with their new name.
  VerifyTableExists(client, database, kRenamedIndex, 10);
  VerifyTableExists(client, database, kRenamedTable, 10);

  // Drop was successful, we should not find these tables.
  VerifyTableNotExists(client, database, kDropTable, 10);
  VerifyTableNotExists(client, database, kDropIndex, 10);

  // Verify that the tables were altered on DocDB.
  // kRenameCol should have 'key' column changed.
  RETURN_NOT_OK(VerifySchema(client, database, kRenameCol, {"key2", "value", "num"}));
  // kAddCol should have an extra column 'value2'.
  RETURN_NOT_OK(VerifySchema(client, database, kAddCol, {"key", "value", "num", "value2"}));
  // kDropCol should be missing the column 'value'.
  RETURN_NOT_OK(VerifySchema(client, database, kDropCol, {"key", "num"}));

  // We must be able to successfully insert duplicates after dropping the constraint.
  RETURN_NOT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES (1), (1)", kDropPk));
  // Revert the above insert to keep tables in the same state for the caller.
  RETURN_NOT_OK(conn->ExecuteFormat("DELETE FROM $0 WHERE key = 1", kDropPk));

  // We cannot insert duplicates after adding the constraint.
  Status s = conn->ExecuteFormat("INSERT INTO $0 VALUES (1), (1)", kAddPk);
  if (s.ok() || s.message().ToBuffer().find("duplicate key value violates unique constraint") ==
      std::string::npos) {
    return STATUS_FORMAT(IllegalState, "Expected duplicate constraint error");
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::VerifyRowsAfterDdlSuccess(PGConn* conn, const int expected_rows) {
  // Verify that all the tables have their data intact.
  static const vector<string>& tables_to_check = {kRenamedTable, kRenameCol, kAddCol, kDropCol,
                                                  kIndexedTable, kAddPk, kDropPk};
  return VerifyTableRows(conn, tables_to_check, expected_rows);
}

Status PgDdlAtomicityTestBase::VerifyAllFailingDdlsNotRolledBack(client::YBClient* client,
                                                                 const string& database) {
  // Verify that all the DDLs that were run as part of 'RunAllDdlsWithErrorInjection' are
  // not yet rolled back.
  // Tables that failed creation will still be around as they have not been rolled back.
  VerifyTableExists(client, database, kCreateTable, 10);
  VerifyTableExists(client, database, kCreateIndex, 10);

  // Failed rename is not rolled back, so we should find the index with the new name.
  VerifyTableExists(client, database, kRenamedIndex, 10);
  VerifyTableExists(client, database, kRenamedTable, 10);

  // DROP does not reflect until the transaction commits, so the tables still exist even though
  // no rollback actually happened.
  VerifyTableExists(client, database, kDropTable, 10);
  VerifyTableExists(client, database, kDropIndex, 10);

  // These tables all underwent alter operations which failed. But their changes should not yet
  // be rolled back.
  // kRenameCol should have a renamed column.
  RETURN_NOT_OK(VerifySchema(client, database, kRenameCol, {"key2", "value", "num"}));
  // kAddCol should have an extra column.
  RETURN_NOT_OK(VerifySchema(client, database, kAddCol, {"key", "value", "num", "value2"}));
  // kDropCol should have a column marked for deletion.
  RETURN_NOT_OK(VerifySchema(client, database, kDropCol, {"key", "value", "num"}, {"value"}));

  // kAddPk and kDropPk should still be in the intermediate phase where a new table DocDB table
  // has been created with the required PK and the old table still exists.
  if (VERIFY_RESULT(
        client->ListTables(kAddPk, false /* exclude_ysql */, database)).size() != 2)
    return STATUS_FORMAT(IllegalState, "Expected 2 tables for $0", kAddPk);
  if (VERIFY_RESULT(
        client->ListTables(kDropPk, false /* exclude_ysql */, database)).size() != 2)
    return STATUS_FORMAT(IllegalState, "Expected 2 tables for $0", kDropPk);
  return Status::OK();
}

Status PgDdlAtomicityTestBase::VerifyAllFailingDdlsRolledBack(PGConn *conn,
                                                              client::YBClient* client,
                                                              const string& database) {
  // Verify that all the DDLs that were run as part of 'RunAllDdlsWithErrorInjection' are
  // rolled back successfully.

  // Failed creates are rolled back, so we should not find the tables.
  VerifyTableNotExists(client, database, kCreateTable, 20);
  VerifyTableNotExists(client, database, kCreateIndex, 20);

  // Table should exist after failed drop.
  VerifyTableExists(client, database, kDropTable, 20);
  VerifyTableExists(client, database, kDropIndex, 20);

  // After failed renames, we should have tables/indexes with the old name.
  VerifyTableExists(client, database, kRenameTable, 20);
  VerifyTableExists(client, database, kRenameIndex, 20);

  // Intermediate tables created for add/drop pk should be dropped.
  RETURN_NOT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(
              kAddPk, false /* exclude_ysql */, database)).size() == 1
          && VERIFY_RESULT(client->ListTables(
              kDropPk, false /* exclude_ysql */, database)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for new DocDB tables to be dropped."));

  // Verify that all the tables still have their old schema after the alter operation failed.
  static const auto tables = {kRenameCol, kAddCol, kDropCol};
  for (const string& table : tables) {
    RETURN_NOT_OK(VerifySchema(client, database, table, {"key", "value", "num"}));
  }

  // Verify that PK constraint is not present on the table where pk was added (and rolled back),
  // i.e. it is possible to insert duplicate rows.
  RETURN_NOT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES (1), (1)", kAddPk));
  // Revert the above insert to keep tables in the same state for the caller.
  RETURN_NOT_OK(conn->ExecuteFormat("DELETE FROM $0 WHERE key = 1", kAddPk));

  // Verify that PK constraint is still present on the table where pk was dropped (and rolled back).
  Status s = conn->ExecuteFormat("INSERT INTO $0 VALUES (1), (1)", kDropPk);
  if (s.ok() || s.message().ToBuffer().find("duplicate key value violates unique constraint") ==
        std::string::npos) {
    return STATUS_FORMAT(IllegalState, "Expected duplicate constraint error");
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::VerifyRowsAfterDdlErrorInjection(PGConn* conn,
                                                                const int expected_rows) {
  // Verify that all the tables have their data intact.
  static const vector<string>& tables_to_check = {kRenameTable, kRenameCol, kAddCol, kDropCol,
                                                  kIndexedTable, kDropTable, kAddPk, kDropPk};
  return VerifyTableRows(conn, tables_to_check, expected_rows);
}

Status PgDdlAtomicityTestBase::VerifySchema(client::YBClient* client,
                                            const string& database_name,
                                            const string& table_name,
                                            const vector<string>& expected_column_names,
                                            const std::set<string>& cols_marked_for_deletion) {
  return LoggedWaitFor([&] {
      return CheckIfSchemaMatches(client, database_name, table_name, expected_column_names,
                                  cols_marked_for_deletion);
  }, MonoDelta::FromSeconds(60), "Wait for schema to match");
}

Result<bool> PgDdlAtomicityTestBase::CheckIfSchemaMatches(client::YBClient* client,
    const string& database_name, const string& table_name,
    const vector<string>& expected_column_names, const std::set<string>& cols_marked_for_deletion) {

  const auto table_id = VERIFY_RESULT(GetTableIdByTableName(client, database_name, table_name));

  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  RETURN_NOT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
  RETURN_NOT_OK(sync.Wait());

  const auto& columns = table_info->schema.columns();

  if (expected_column_names.size() != columns.size()) {
    LOG(INFO) << "Expected " << expected_column_names.size() << " columns for " << table_name
              << " but found " << columns.size() << " columns. Table Schema: "
              << table_info->schema.ToString();
    return false;
  }

  size_t num_cols_marked_for_deletion = 0;
  for (size_t i = 0; i < expected_column_names.size(); ++i) {
    if (columns[i].name().compare(expected_column_names[i]) != 0) {
      LOG(INFO) << "Expected column " << expected_column_names[i] << " but found "
                << columns[i].name();
      return false;
    }
    if (columns[i].marked_for_deletion()) {
      ++num_cols_marked_for_deletion;
      if (cols_marked_for_deletion.find(columns[i].name()) == cols_marked_for_deletion.end()) {
        LOG(INFO) << "Column " << columns[i].name() << " is unexpectedly marked for deletion";
        return false;
      }
    }
  }
  if (cols_marked_for_deletion.size() != num_cols_marked_for_deletion) {
    LOG(INFO) << Format("Expected $0 columns marked for deletion for table $1 but found $2 "
                        "columns. Table schema: $3", cols_marked_for_deletion.size(),
                        table_name, num_cols_marked_for_deletion, table_info->schema.ToString());
    return false;
  }
  return true;
}

Status PgDdlAtomicityTestBase::VerifyTableRows(PGConn* conn,
                                               const vector<string>& tables,
                                               const int expected_rows) {
  for (const auto& table : tables) {
    PGResultPtr res = VERIFY_RESULT(conn->FetchFormat("SELECT * FROM $0", table));
    const int num_rows = PQntuples(res.get());
    if (num_rows != expected_rows) {
      return STATUS_FORMAT(IllegalState,
          "Unexpected number of rows $1 in table $0", table, num_rows);
    }
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::WaitForDdlVerificationAfterSuccessfulDdl(
    client::YBClient* client,
    const string& database) {

  client::VerifyTableNotExists(client, database, kDropTable, 20);
  client::VerifyTableExists(client, database, kRenamedIndex, 20);
  client::VerifyTableNotExists(client, database, kDropIndex, 20);
  // Adding/Dropping a primary key creates a new DocDB table. The old table will be deleted after
  // the DDL transaction is committed.
  RETURN_NOT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(
              kAddPk, false /* exclude_ysql */, database)).size() == 1
          && VERIFY_RESULT(client->ListTables(
              kDropPk, false /* exclude_ysql */, database)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for old DocDB tables to be dropped."));

  static const vector<string> tables = {kRenameCol, kAddCol, kDropCol, kCreateTable, kCreateIndex,
      kRenamedTable, kRenamedIndex, kAddPk, kDropPk};

  for (size_t i = 0; i< tables.size(); ++i) {
    RETURN_NOT_OK(WaitForDdlVerification(client, database, tables[i]));
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::WaitForDdlVerificationAfterDdlFailure(
    client::YBClient* client,
    const string& database) {

  client::VerifyTableNotExists(client, database, kCreateTable, 20);
  client::VerifyTableNotExists(client, database, kCreateIndex, 20);
  RETURN_NOT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(
              kAddPk, false /* exclude_ysql */, database)).size() == 1
          && VERIFY_RESULT(client->ListTables(
              kDropPk, false /* exclude_ysql */, database)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for new tables to be dropped."));

  static const vector<string> tables = {kRenameCol, kAddCol, kDropCol, kDropTable, kDropIndex,
      kRenameTable, kRenameIndex, kAddPk, kDropPk};

  for (size_t i = 0; i< tables.size(); ++i) {
    RETURN_NOT_OK(WaitForDdlVerification(client, database, tables[i]));
  }
  return Status::OK();
}

Status PgDdlAtomicityTestBase::WaitForDdlVerification(client::YBClient* client,
                                                      const string& database_name,
                                                      const string& table) {
  return LoggedWaitFor([&] {
      return CheckIfDdlVerificationComplete(client, database_name, table);
    }, MonoDelta::FromSeconds(60), "Wait for DDL verification to finish for table " + table);
}

Result<bool> PgDdlAtomicityTestBase::CheckIfDdlVerificationComplete(
    client::YBClient* client,
    const string& database_name,
    const string& table_name) {

  auto res = GetTableIdByTableName(client, database_name, table_name);
  if (!res && res.status().IsNotFound()) {
    // Probably this table is undergoing rename and is not found. Wait for Ddl verification to
    // finish so that it has the correct name.
    return false;
  }

  auto table_id = VERIFY_RESULT(std::move(res));

  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;

  RETURN_NOT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
  auto s = sync.Wait();
  if (!s.ok() && s.IsNotFound()) {
    // Probably this table is undergoing rename and is not found. Wait for Ddl verification to
    // finish so that it has the correct name.
    return false;
  }

  if (table_info->ysql_ddl_txn_verifier_state) {
    DCHECK_EQ(table_info->ysql_ddl_txn_verifier_state->size(), 1);
    LOG(INFO) << "DDL verification still in progress for table " << table_name
              << " with state "
              << table_info->ysql_ddl_txn_verifier_state->Get(0).ShortDebugString();
    return false;
  }

  return true;
}

Status PgDdlAtomicityTestBase::ExecuteWithRetry(PGConn *conn, const string& ddl) {
  const int kDefaultNumRetries = 5;
  for (size_t num_retries = 0; num_retries < kDefaultNumRetries; ++num_retries) {
    auto s = conn->Execute(ddl);
    if (s.ok() || !IsDdlVerificationError(s.ToString())) {
      return s;
    }
    // Sleep before retrying again.
    sleep(1);
  }
  return STATUS_FORMAT(IllegalState, "Failed to execute DDL statement $0", ddl);
}

} // namespace yb::pgwrapper
