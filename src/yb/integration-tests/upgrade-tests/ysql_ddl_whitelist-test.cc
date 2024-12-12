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

DECLARE_bool(TEST_ysql_require_force_catalog_modifications);

#define ASSERT_STMT_OK(stmt) ASSERT_OK(conn.Execute(stmt))

#define ASSERT_DDL_NOK(stmt) ASSERT_NOK_STR_CONTAINS(conn.Execute(stmt), kExpectedDdlError)

namespace yb {

constexpr auto kExpectedDdlError =
    "Catalog update without force_catalog_modifications when "
    "TEST_ysql_require_force_catalog_modifications is set";

using YsqlDdlWhitelistTest = pgwrapper::PgMiniTestBase;

TEST_F(YsqlDdlWhitelistTest, TestDDLBlocking) {
  // Prepare the cluster.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_STMT_OK("CREATE TABLE normal_table (id INT)");

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
  ASSERT_DDL_NOK("ALTER TABLE test_temp_table_with_pk ADD COLUMN b INT");

  // Drop a collection of temp tables.
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table (id INT)");
  ASSERT_STMT_OK("DROP TABLE test_temp_table, test_temp_table_with_pk");

  // Ensure normal DDLs are not allowed.
  ASSERT_DDL_NOK("CREATE TABLE normal_table2 (id INT)");
  ASSERT_DDL_NOK("CREATE TABLE normal_table2 (id INT PRIMARY KEY)");
  ASSERT_DDL_NOK("DROP TABLE normal_table");
  ASSERT_DDL_NOK("ALTER TABLE normal_table ADD COLUMN b INT");

  // Ensure mix of temp and normal table drops are not allowed.
  ASSERT_STMT_OK("CREATE TEMP TABLE test_temp_table (id INT)");
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
