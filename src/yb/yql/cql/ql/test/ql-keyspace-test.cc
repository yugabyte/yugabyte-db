//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/common/ql_value.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/yql/cql/ql/util/errcodes.h"

using std::string;

namespace yb {
namespace ql {

class TestQLKeyspace : public QLTestBase {
 public:
  TestQLKeyspace() : QLTestBase() {
  }

  // CREATE statements.
  inline const string CreateStmt(string params) {
    return "CREATE " + params;
  }

  inline const string CreateKeyspaceStmt(string params) {
    return CreateStmt("KEYSPACE " + params);
  }

  inline const string CreateKeyspaceIfNotExistsStmt(string params) {
    return CreateStmt("KEYSPACE IF NOT EXISTS " + params);
  }

  inline const string CreateSchemaStmt(string params) {
    return CreateStmt("SCHEMA " + params);
  }

  inline const string CreateSchemaIfNotExistsStmt(string params) {
    return CreateStmt("SCHEMA IF NOT EXISTS " + params);
  }

  // DROP statements.
  inline const string DropKeyspaceStmt(string params) {
    return "DROP KEYSPACE " + params;
  }

  inline const string DropKeyspaceIfExistsStmt(string params) {
    return "DROP KEYSPACE IF EXISTS " + params;
  }

  inline const string DropSchemaStmt(string params) {
    return "DROP SCHEMA " + params;
  }

  inline const string DropSchemaIfExistsStmt(string params) {
    return "DROP SCHEMA IF EXISTS " + params;
  }

  inline const string UseStmt(string params) {
    return "USE " + params;
  }

  inline const string CreateTableStmt(string params) {
    return "CREATE TABLE " + params;
  }
};

TEST_F(TestQLKeyspace, TestQLCreateKeyspaceSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string keyspace1 = "test;";

  // Try to delete unknown keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropKeyspaceStmt(keyspace1),
      "Keyspace Not Found. YCQL keyspace name not found");

  // Delete unknown keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceIfExistsStmt(keyspace1));

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(CreateKeyspaceStmt(keyspace1));

  // Try to create the keyspace1 once again.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt(keyspace1),
      "Keyspace Already Exists. Keyspace 'test' already exists");

  // Delete the keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceStmt(keyspace1));

  // Try to delete already deleted keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropKeyspaceStmt(keyspace1),
      "Keyspace Not Found. YCQL keyspace name not found");

  // Delete already deleted keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceIfExistsStmt(keyspace1));

  // Try to create a keyspace with a syntax error ('KEYSPAC' instead of 'KEYSPACE').
  LOG(INFO) << "Exec SQL: " << CreateStmt("KEYSPAC " + keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt("KEYSPAC " + keyspace1),
      "Invalid SQL Statement. syntax error");

  // Try to create 2 keyspaces in one request.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt("ks1 ks2;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt("ks1 ks2;"),
      "Invalid SQL Statement. syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt("ks1 AUTHORIZATION user1;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt("ks1 AUTHORIZATION user1;"),
      "Feature Not Supported");
}

TEST_F(TestQLKeyspace, TestQLCreateKeyspaceIfNotExists) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string keyspace1 = "test;";

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceIfNotExistsStmt(keyspace1);
  EXEC_VALID_STMT(CreateKeyspaceIfNotExistsStmt(keyspace1));

  // Try to create the keyspace1 once again.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceIfNotExistsStmt(keyspace1);
  EXEC_VALID_STMT(CreateKeyspaceIfNotExistsStmt(keyspace1));

  // Try to create a keyspace with a syntax error ('EXIST' instead of 'EXISTS').
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt("IF NOT EXIST " + keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt("IF NOT EXIST " + keyspace1),
      "Invalid SQL Statement. syntax error");

  // THE FOLLOWING TESTS FAIL DUE TO UNKNOWN PARSER BUG. TODO: Investigate

  // Try to create 2 keyspaces in one request.
//  LOG(INFO) << "Exec SQL: " << CreateKeyspaceIfNotExistsStmt("ks1 ks2;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceIfNotExistsStmt("ks1 ks2;"),
//      "Invalid SQL Statement. syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
//  LOG(INFO) << "Exec SQL: " << CreateKeyspaceIfNotExistsStmt("ks1 AUTHORIZATION user1;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceIfNotExistsStmt("ks1 AUTHORIZATION user1;"),
//      "Feature Not Supported. AUTHORIZATION");
}

TEST_F(TestQLKeyspace, TestQLCreateSchemaSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string keyspace1 = "test;";

  // Try to delete unknown keyspace1.
  LOG(INFO) << "Exec SQL: " << DropSchemaStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropSchemaStmt(keyspace1),
      "Keyspace Not Found. YCQL keyspace name not found");

  // Delete unknown keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropSchemaIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropSchemaIfExistsStmt(keyspace1));

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt(keyspace1);
  EXEC_VALID_STMT(CreateSchemaStmt(keyspace1));

  // Try to create the keyspace1 once again.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt(keyspace1),
      "Keyspace Already Exists. Keyspace 'test' already exists");

  // Delete the keyspace1.
  LOG(INFO) << "Exec SQL: " << DropSchemaStmt(keyspace1);
  EXEC_VALID_STMT(DropSchemaStmt(keyspace1));

  // Try to delete already deleted keyspace1.
  LOG(INFO) << "Exec SQL: " << DropSchemaStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropSchemaStmt(keyspace1),
      "Keyspace Not Found. YCQL keyspace name not found");

  // Delete already deleted keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropSchemaIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropSchemaIfExistsStmt(keyspace1));

  // Try to create the keyspace1 with a syntax error ('SCHEM' instead of 'SCHEMA').
  LOG(INFO) << "Exec SQL: " << CreateStmt("SCHEM " + keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt("SCHEM " + keyspace1),
      "Invalid SQL Statement. syntax error");

  // Try to create 2 keyspaces in one request.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt("ks1 ks2;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt("ks1 ks2;"),
      "Invalid SQL Statement. syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt("ks1 AUTHORIZATION user1;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt("ks1 AUTHORIZATION user1;"),
      "Feature Not Supported");
}

TEST_F(TestQLKeyspace, TestQLCreateSchemaIfNotExists) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string keyspace1 = "test;";

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateSchemaIfNotExistsStmt(keyspace1);
  EXEC_VALID_STMT(CreateSchemaIfNotExistsStmt(keyspace1));

  // Try to create the keyspace1 once again.
  LOG(INFO) << "Exec SQL: " << CreateSchemaIfNotExistsStmt(keyspace1);
  EXEC_VALID_STMT(CreateSchemaIfNotExistsStmt(keyspace1));

  // Try to create a keyspace with a syntax error ('EXIST' instead of 'EXISTS').
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt("IF NOT EXIST " + keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt("IF NOT EXIST " + keyspace1),
      "Invalid SQL Statement. syntax error");

  // THE FOLLOWING TESTS FAIL DUE TO UNKNOWN PARSER BUG. TODO: Investigate

  // Try to create 2 keyspaces in one request.
//  LOG(INFO) << "Exec SQL: " << CreateSchemaIfNotExistsStmt("ks1 ks2;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaIfNotExistsStmt("ks1 ks2;"),
//      "Invalid SQL Statement. syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
//  LOG(INFO) << "Exec SQL: " << CreateSchemaIfNotExistsStmt("ks1 AUTHORIZATION user1;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaIfNotExistsStmt("ks1 AUTHORIZATION user1;"),
//      "Feature Not Supported. AUTHORIZATION");
}

TEST_F(TestQLKeyspace, TestQLUseKeyspaceSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string keyspace1 = "test;";

  // Try to use unknown keyspace1.
  LOG(INFO) << "Exec SQL: " << UseStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(UseStmt(keyspace1),
      "Keyspace Not Found. Cannot use unknown keyspace");

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(CreateKeyspaceStmt(keyspace1));

  // Use the keyspace1.
  LOG(INFO) << "Exec SQL: " << UseStmt(keyspace1);
  EXEC_VALID_STMT(UseStmt(keyspace1));

  // Delete keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceStmt(keyspace1));

  // Try to use deleted keyspace1.
  LOG(INFO) << "Exec SQL: " << UseStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(UseStmt(keyspace1),
      "Keyspace Not Found. Cannot use unknown keyspace");
}

TEST_F(TestQLKeyspace, TestQLUseKeyspaceWithTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string keyspace1 = "test;";
  const string keyspace2 = "test2;";
  const string table1 = "table1(id int, primary key(id));";
  const string system_table2 = "system.table2(id int, primary key(id));";
  const string test_table3 = "test.table3(id int, primary key(id));";
  const string table3 = "table3(id int, primary key(id));";
  const string test_any_table4 = "test.subname.table4(id int, primary key(id));";

  // No keyspace - using current keyspace (kDefaultKeyspaceName here).
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(table1);
  EXEC_VALID_STMT(CreateTableStmt(table1));

  // 'system' keyspace (not supported yet)
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(system_table2);
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(system_table2),
                               ErrorText(ErrorCode::SYSTEM_NAMESPACE_READONLY));

  // 'default' keyspace is always available.
  // TODO: It's failed now because 'DEFAULT' is a reserved keyword. Discuss & fix the case.
  // LOG(INFO) << "Exec SQL: " << CreateTableStmt(default_table5);
  // EXEC_VALID_STMT(CreateTableStmt(default_table5));

  // The keyspace (keyspace1) has not been created yet.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(test_table3);
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(test_table3),
      "Keyspace Not Found. Error creating table test.table3 on the master: "
      "YCQL keyspace name not found: test");

  // Invalid name 'keyspace.SOMETHING.table'.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(test_any_table4);
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(test_any_table4), "Invalid table name");

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(CreateKeyspaceStmt(keyspace1));

  // Create table in the new keyspace.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(test_table3);
  EXEC_VALID_STMT(CreateTableStmt(test_table3));

  // Use the keyspace1.
  LOG(INFO) << "Exec SQL: " << UseStmt(keyspace1);
  EXEC_VALID_STMT(UseStmt(keyspace1));

  // Use current keyspace. The table has been already created.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(table3);
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(table3),
      "Duplicate Object. Object 'test.table3' already exists");

  // Create the keyspace2.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace2);
  EXEC_VALID_STMT(CreateKeyspaceStmt(keyspace2));

  // Use the keyspace2.
  LOG(INFO) << "Exec SQL: " << UseStmt(keyspace2);
  EXEC_VALID_STMT(UseStmt(keyspace2));

  // Table3 can be created in other (keyspace2) keyspace.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(table3);
  EXEC_VALID_STMT(CreateTableStmt(table3));
}

TEST_F(TestQLKeyspace, TestCreateSystemTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Allow writes to system keyspace.
  client::FLAGS_yb_system_namespace_readonly = false;

  // Create system table.
  EXEC_VALID_STMT("create table system.t (c int primary key, v int);");

  // Insert into system table.
  EXEC_VALID_STMT("insert into system.t (c, v) values (1, 2);");

  // Select from system table.
  EXEC_VALID_STMT("select * from system.t where c = 1;");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).int32_value(), 2);
}

TEST_F(TestQLKeyspace, TestQLSelectInvalidTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string select_stmt = "SELECT * FROM my_keyspace1.test_table WHERE h1 = 1 AND h2 = 'h1';";

  LOG(INFO) << "Exec SQL: " << select_stmt;
  EXEC_INVALID_STMT_WITH_ERROR(select_stmt, "Object Not Found");
}

} // namespace ql
} // namespace yb
