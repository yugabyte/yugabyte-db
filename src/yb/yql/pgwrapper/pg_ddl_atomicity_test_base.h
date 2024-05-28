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

#pragma once

#include <string>
#include "yb/client/client.h"
#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/util/status.h"

using namespace std::literals;

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(DdlErrorInjection);

class PgDdlAtomicityTestBase : public LibPqTestBase {
 protected:
  // DDL helper functions.
  static std::string CreateTableStmt(const std::string& tablename,
      const std::string& tablegroup_name = "") {

    return "CREATE TABLE " + tablename + " (key INT PRIMARY KEY, value text, num real)" +
           (tablegroup_name.empty() ? "" : " TABLEGROUP " + tablegroup_name);
  }

  static std::string CreateTableWithoutPk(const std::string& tablename,
      const std::string& tablegroup_name = "") {

    return "CREATE TABLE " + tablename + " (key INT, value text, num real)" +
           (tablegroup_name.empty() ? "" : " TABLEGROUP " + tablegroup_name);
  }

  std::string RenameTableStmt(const std::string& tablename) {
    return RenameTableStmt(tablename, kRenamedTable);
  }

  static std::string RenameTableStmt(const std::string& tablename,
      const std::string& table_new_name) {

    return Format("ALTER TABLE $0 RENAME TO $1", tablename, table_new_name);
  }

  static std::string AddColumnStmt(const std::string& tablename,
      const std::string& col_name_to_add = "value2", const std::string& default_val = "") {

    return default_val.empty() ?
        Format("ALTER TABLE $0 ADD COLUMN $1 TEXT", tablename, col_name_to_add) :
        Format("ALTER TABLE $0 ADD COLUMN $1 TEXT DEFAULT '$2'",
            tablename, col_name_to_add, default_val);
  }

  static std::string DropColumnStmt(const std::string& tablename,
      const std::string& col = "value") {

    return Format("ALTER TABLE $0 DROP COLUMN $1", tablename, col);
  }

  static std::string AddDropColumnStmt(const std::string& tablename,
      const std::string& col_to_add, const std::string& col_to_drop) {

    return Format("ALTER TABLE $0 DROP COLUMN $1, ADD COLUMN $2 text",
                  tablename, col_to_drop, col_to_add);
  }

  static std::string RenameColumnStmt(const std::string& tablename) {
    return RenameColumnStmt(tablename, "key", "key2");
  }

  static std::string RenameColumnStmt(const std::string& tablename,
      const std::string& col_to_rename, const std::string& col_new_name) {

    return Format("ALTER TABLE $0 RENAME COLUMN $1 TO $2", tablename, col_to_rename, col_new_name);
  }

  static std::string DropTableStmt(const std::string& tablename) {
    return "DROP TABLE " + tablename;
  }

  static std::string CreateIndexStmt(const std::string& idxname, const std::string& tablename) {
    return CreateIndexStmt(idxname, tablename, "value");
  }

  static std::string CreateIndexStmt(const std::string& idxname,
                                     const std::string& table,
                                     const std::string& col) {
    return Format("CREATE UNIQUE INDEX $0 ON $1($2)", idxname, table, col);
  }

  static std::string RenameIndexStmt(const std::string& idxname, const std::string& idx_new_name) {
    return Format("ALTER INDEX $0 RENAME TO $1", idxname, idx_new_name);
  }

  static std::string DropIndexStmt(const std::string& idxname) {
    return Format("DROP INDEX $0", idxname);
  }

  static std::string AddPkStmt(const std::string& tablename) {
    return Format("ALTER TABLE $0 ADD PRIMARY KEY(key)", tablename);
  }

  static std::string DropPkStmt(const std::string& tablename) {
    return Format("ALTER TABLE $0 DROP CONSTRAINT $0_pkey", tablename);
  }

  // Get DDL statements that modify both PG catalog and DocDB schema.
  std::vector<std::string> GetAllDDLs();

  // Create all the tables that will be required for the DDL atomicity tests.
  Status SetupTablesForAllDdls(PGConn *conn,
                               const int num_rows_to_insert,
                               const std::string& tablegroup = "");

  Status SetupTables(PGConn *conn,
                     const std::vector<TableName>& tables,
                     const std::vector<TableName>& indexes,
                     const int num_rows_to_insert,
                     const std::string& tablegroup_name = "");

  // Run all possible tests supported by this test.
  Status RunAllDdls(PGConn* conn);
  // Run all possible DDLs supported by this test with error injection. These DDLs will all fail and
  // initiate rollback on the YB-Master.
  Status RunAllDdlsWithErrorInjection(PGConn* conn);

  // API that can be used after 'RunAllDdls' above to wait for all the DDL verification to
  // complete.
  Status WaitForDdlVerificationAfterSuccessfulDdl(
      client::YBClient* client, const std::string& database);

  // API that can be used after 'RunAllDdlsWithErrorInjection' above to wait for all the DDL
  // verification to complete.
  Status WaitForDdlVerificationAfterDdlFailure(
      client::YBClient* client, const std::string& database);

  // Wait for ddl verification to complete on 'table_name' in 'database_name'.
  Status WaitForDdlVerification(client::YBClient* client,
                                const std::string& database_name,
                                const std::string& table_name);

  // Work-horse for WaitForDdlVerification variants.
  Result<bool> CheckIfDdlVerificationComplete(
      client::YBClient* client, const std::string& database_name, const std::string& table_name);

  // Verify that all the tables that underwent DDLs successfully are in expected state.
  Status VerifyAllSuccessfulDdls(PGConn* conn, client::YBClient* client,
                                 const std::string& database);
  // Verify that all the tables that underwent DDLs with error injection but not yet completed
  // verification are in the expected state.
  Status VerifyAllFailingDdlsNotRolledBack(client::YBClient* client, const std::string& database);
  // Verify that all the tables that underwent DDLs with error injection and completed verification
  // are in the expected state.
  Status VerifyAllFailingDdlsRolledBack(PGConn* conn, client::YBClient* client,
                                        const std::string& database);

  // Verify that 'table_name' has the 'expected_column_names' in the given order. The cols in
  // 'cols_marked_for_deletion' are expected to be found in the same order in the schema and should
  // all be marked for deletion.
  Status VerifySchema(client::YBClient* client,
                      const std::string& database_name,
                      const std::string& table_name,
                      const std::vector<std::string>& expected_column_names,
                      const std::set<std::string>& cols_marked_for_deletion = {});

  // Work-horse for VerifySchema variants.
  Result<bool> CheckIfSchemaMatches(client::YBClient* client,
                                    const std::string& database_name,
                                    const std::string& table_name,
                                    const std::vector<std::string>& expected_column_names,
                                    const std::set<std::string>& cols_marked_for_deletion);

  // Verify that the 'table_name' has the 'expected_replica_identity'
  Status VerifyReplicaIdentityMatches(client::YBClient* client,
                                      const std::string& database_name,
                                      const std::string& table_name,
                                      PgReplicaIdentity expected_replica_identity);

  Result<bool> CheckIfReplicaIdentityMatches(client::YBClient* client,
                                             const std::string& database_name,
                                             const std::string& table_name,
                                             PgReplicaIdentity expected_replica_identity);

  Status VerifyRowsAfterDdlSuccess(PGConn* conn, const int expected_rows);

  Status VerifyRowsAfterDdlErrorInjection(PGConn* conn, const int expected_rows);

  // Verify that the table has the expected number of rows.
  Status VerifyTableRows(PGConn* conn,
                         const std::vector<std::string>& tablename,
                         const int expected_rows);


  bool IsDdlVerificationError(const std::string& error) {
    return error.find(kDdlVerificationError) != std::string::npos;
  }

  Status ExecuteWithRetry(PGConn *conn, const std::string& ddl);

  // Table names for the corresponding DDL tests used by this class.
  const std::string kRenameCol = "rename_col_test";
  const std::string kRenamedTable = "foobar";
  const std::string kRenamedIndex = "foobar_idx";
  const std::string kAddCol = "add_col_test";
  const std::string kDropCol = "drop_col_test";

  const std::string kCreateTable = "create_test";
  const std::string kRenameTable = "rename_table_test";
  const std::string kDropTable = "drop_test";

  const std::string kIndexedTable = "indexed_table_test";
  const std::string kCreateIndex = "create_index_test";
  const std::string kRenameIndex = "rename_index_test";
  const std::string kDropIndex = "drop_index_test";
  const std::string kAddPk = "add_pk_test";
  const std::string kDropPk = "drop_pk_test";

  const std::string kDatabase = "yugabyte";
  constexpr static std::string_view kDdlVerificationError =
      "TABLE_SCHEMA_CHANGE_IN_PROGRESS"sv;
};

} // namespace yb::pgwrapper
