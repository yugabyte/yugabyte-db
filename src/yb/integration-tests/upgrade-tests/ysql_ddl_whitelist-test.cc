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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_string(allowed_preview_flags_csv);

DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);

DECLARE_bool(TEST_ysql_require_force_catalog_modifications);

#define ASSERT_STMT_OK(stmt) ASSERT_OK(conn.Execute(stmt))

#define ASSERT_DDL_NOK(stmt) ASSERT_NOK_STR_CONTAINS(conn.Execute(stmt), kExpectedDdlError)

namespace yb {

constexpr auto kExpectedDdlError =
    "Catalog update without force_catalog_modifications when "
    "TEST_ysql_require_force_catalog_modifications is set";

class YsqlDdlWhitelistTest : public pgwrapper::PgMiniTestBase {
 protected:
  void SetUp() override {
    // TODO(#28742): Fix interaction of ysql_yb_ddl_transaction_block_enabled with
    // yb_force_catalog_update_on_next_ddl. For now, disable table locks for this test.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = false;
    pgwrapper::PgMiniTestBase::SetUp();
  }
};

TEST_F(YsqlDdlWhitelistTest, TestDDLBlocking) {
  // Prepare the cluster.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_STMT_OK("CREATE TABLE normal_table (id INT, b INT)");
  ASSERT_STMT_OK("CREATE INDEX normal_idx ON normal_table (b)");

  // Block non temp DDLs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_require_force_catalog_modifications) = true;

  // Ensure temp table DDLs are allowed.
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table (id INT)");

  // Drop the connection and start a new one to ensure cleanup of the temp table.
  conn = ASSERT_RESULT(Connect());
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table (id INT)");
  ASSERT_STMT_OK("DROP TABLE test_temp_table");

  // Temp table with an index.
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table_with_pk (id INT PRIMARY KEY)");
  ASSERT_STMT_OK("ALTER TABLE test_temp_table_with_pk ADD COLUMN b INT");
  ASSERT_STMT_OK("CREATE INDEX temp_idx ON test_temp_table_with_pk (b)");
  ASSERT_STMT_OK("DROP INDEX temp_idx");

  // Truncate a temp table.
  ASSERT_STMT_OK("TRUNCATE TABLE test_temp_table_with_pk");

  // Drop a collection of temp tables.
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table (id INT)");
  ASSERT_STMT_OK("DROP TABLE test_temp_table, test_temp_table_with_pk");

  // Ensure normal DDLs are not allowed.
  ASSERT_DDL_NOK("CREATE TABLE normal_table2 (id INT)");
  ASSERT_DDL_NOK("CREATE TABLE normal_table2 (id INT PRIMARY KEY)");
  ASSERT_DDL_NOK("DROP TABLE normal_table");
  ASSERT_DDL_NOK("ALTER TABLE normal_table ADD COLUMN c INT");
  ASSERT_DDL_NOK("CREATE INDEX normal_idx2 ON normal_table (b)");
  ASSERT_DDL_NOK("DROP INDEX normal_idx");
  ASSERT_DDL_NOK("TRUNCATE TABLE normal_table");

  // Ensure mix of temp and normal table truncate and drop are not allowed.
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table (id INT)");
  ASSERT_DDL_NOK("TRUNCATE TABLE normal_table, test_temp_table");
  ASSERT_DDL_NOK("DROP TABLE normal_table, test_temp_table");
  ASSERT_DDL_NOK("DROP TABLE test_temp_table, normal_table");

  // Ensure yb_force_catalog_update_on_next_ddl works as expected.
  ASSERT_STMT_OK("SET yb_force_catalog_update_on_next_ddl = true");
  ASSERT_STMT_OK("CREATE TABLE normal_table2 (id INT)");
  // Only 1 DDL should work.
  ASSERT_DDL_NOK("CREATE TABLE normal_table3 (id INT)");

  ASSERT_STMT_OK("SET yb_force_catalog_update_on_next_ddl = true");
  ASSERT_STMT_OK("ALTER TABLE normal_table2 ADD COLUMN b INT");
}

}  // namespace yb
