//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

#define EXEC_INVALID_STMT_WITH_ERROR(sql_stmt, expected_error, expected_error_msg)                 \
do {                                                                                               \
  Status s = processor->Run(sql_stmt);                                                             \
  EXPECT_FALSE(s.ok()) << s.ToString();                                                            \
  const auto expected_error_copy = (expected_error);                                               \
  const auto expected_error_msg_copy = (expected_error_msg);                                       \
  if (!std::string(expected_error_copy).empty()) {                                                 \
    EXPECT_FALSE(s.ToString().find(expected_error_copy) == string::npos) << s.ToString();          \
  }                                                                                                \
  if (!std::string(expected_error_msg_copy).empty()) {                                             \
    EXPECT_FALSE(s.ToString().find(expected_error_msg_copy) == string::npos) << s.ToString();      \
  }                                                                                                \
} while (false)

namespace yb {
namespace sql {

class YbSqlKeyspace : public YbSqlTestBase {
 public:
  YbSqlKeyspace() : YbSqlTestBase() {
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

TEST_F(YbSqlKeyspace, TestSqlCreateKeyspaceSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  const string keyspace1 = "test;";

  // Try to delete unknown keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropKeyspaceStmt(keyspace1), "Keyspace Not Found",
      "The namespace does not exist");

  // Delete unknown keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceIfExistsStmt(keyspace1));

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(CreateKeyspaceStmt(keyspace1));

  // Try to create the keyspace1 once again.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt(keyspace1), "Keyspace Already Exists",
      "Already present");

  // Delete the keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceStmt(keyspace1));

  // Try to delete already deleted keyspace1.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropKeyspaceStmt(keyspace1), "Keyspace Not Found",
      "The namespace does not exist");

  // Delete already deleted keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropKeyspaceIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropKeyspaceIfExistsStmt(keyspace1));

  // Try to create a keyspace with a syntax error ('KEYSPAC' instead of 'KEYSPACE').
  LOG(INFO) << "Exec SQL: " << CreateStmt("KEYSPAC " + keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt("KEYSPAC " + keyspace1),
      "Invalid SQL Statement", "syntax error");

  // Try to create 2 keyspaces in one request.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt("ks1 ks2;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt("ks1 ks2;"),
      "Invalid SQL Statement", "syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
  LOG(INFO) << "Exec SQL: " << CreateKeyspaceStmt("ks1 AUTHORIZATION user1;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceStmt("ks1 AUTHORIZATION user1;"),
      "Feature Not Supported", "AUTHORIZATION");
}

TEST_F(YbSqlKeyspace, TestSqlCreateKeyspaceIfNotExists) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

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
      "Invalid SQL Statement", "syntax error");

  // THE FOLLOWING TESTS FAIL DUE TO UNKNOWN PARSER BUG. TODO: Investigate

  // Try to create 2 keyspaces in one request.
//  LOG(INFO) << "Exec SQL: " << CreateKeyspaceIfNotExistsStmt("ks1 ks2;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceIfNotExistsStmt("ks1 ks2;"),
//      "Invalid SQL Statement", "syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
//  LOG(INFO) << "Exec SQL: " << CreateKeyspaceIfNotExistsStmt("ks1 AUTHORIZATION user1;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateKeyspaceIfNotExistsStmt("ks1 AUTHORIZATION user1;"),
//      "Feature Not Supported", "AUTHORIZATION");
}

TEST_F(YbSqlKeyspace, TestSqlCreateSchemaSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  const string keyspace1 = "test;";

  // Try to delete unknown keyspace1.
  LOG(INFO) << "Exec SQL: " << DropSchemaStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropSchemaStmt(keyspace1), "Keyspace Not Found",
      "The namespace does not exist");

  // Delete unknown keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropSchemaIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropSchemaIfExistsStmt(keyspace1));

  // Create the keyspace1.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt(keyspace1);
  EXEC_VALID_STMT(CreateSchemaStmt(keyspace1));

  // Try to create the keyspace1 once again.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt(keyspace1), "Keyspace Already Exists",
      "Already present");

  // Delete the keyspace1.
  LOG(INFO) << "Exec SQL: " << DropSchemaStmt(keyspace1);
  EXEC_VALID_STMT(DropSchemaStmt(keyspace1));

  // Try to delete already deleted keyspace1.
  LOG(INFO) << "Exec SQL: " << DropSchemaStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(DropSchemaStmt(keyspace1), "Keyspace Not Found",
      "The namespace does not exist");

  // Delete already deleted keyspace1 BUT with IF EXISTS.
  LOG(INFO) << "Exec SQL: " << DropSchemaIfExistsStmt(keyspace1);
  EXEC_VALID_STMT(DropSchemaIfExistsStmt(keyspace1));

  // Try to create the keyspace1 with a syntax error ('SCHEM' instead of 'SCHEMA').
  LOG(INFO) << "Exec SQL: " << CreateStmt("SCHEM " + keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt("SCHEM " + keyspace1),
      "Invalid SQL Statement", "syntax error");

  // Try to create 2 keyspaces in one request.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt("ks1 ks2;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt("ks1 ks2;"),
      "Invalid SQL Statement", "syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
  LOG(INFO) << "Exec SQL: " << CreateSchemaStmt("ks1 AUTHORIZATION user1;");
  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaStmt("ks1 AUTHORIZATION user1;"),
      "Feature Not Supported", "AUTHORIZATION");
}

TEST_F(YbSqlKeyspace, TestSqlCreateSchemaIfNotExists) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

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
      "Invalid SQL Statement", "syntax error");

  // THE FOLLOWING TESTS FAIL DUE TO UNKNOWN PARSER BUG. TODO: Investigate

  // Try to create 2 keyspaces in one request.
//  LOG(INFO) << "Exec SQL: " << CreateSchemaIfNotExistsStmt("ks1 ks2;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaIfNotExistsStmt("ks1 ks2;"),
//      "Invalid SQL Statement", "syntax error");

  // Try to create a keyspaces with unsupported AUTHORIZATION keyword.
//  LOG(INFO) << "Exec SQL: " << CreateSchemaIfNotExistsStmt("ks1 AUTHORIZATION user1;");
//  EXEC_INVALID_STMT_WITH_ERROR(CreateSchemaIfNotExistsStmt("ks1 AUTHORIZATION user1;"),
//      "Feature Not Supported", "AUTHORIZATION");
}

TEST_F(YbSqlKeyspace, TestSqlUseKeyspaceSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  const string keyspace1 = "test;";

  // Try to use unknown keyspace1.
  LOG(INFO) << "Exec SQL: " << UseStmt(keyspace1);
  EXEC_INVALID_STMT_WITH_ERROR(UseStmt(keyspace1), "Keyspace Not Found",
      "Cannot use unknown keyspace");

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
  EXEC_INVALID_STMT_WITH_ERROR(UseStmt(keyspace1), "Keyspace Not Found",
      "Cannot use unknown keyspace");
}

TEST_F(YbSqlKeyspace, TestSqlUseKeyspaceWithTable) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

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
                               ErrorText(ErrorCode::SYSTEM_NAMESPACE_READONLY), "");

  // 'default' keyspace is always available.
  // TODO: It's failed now because 'DEFAULT' is a reserved keyword. Discuss & fix the case.
  // LOG(INFO) << "Exec SQL: " << CreateTableStmt(default_table5);
  // EXEC_VALID_STMT(CreateTableStmt(default_table5));

  // The keyspace (keyspace1) has not been created yet.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(test_table3);
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(test_table3), "Invalid Table Definition",
      "Invalid namespace id or namespace name");

  // Invalid name 'keyspace.SOMETHING.table'.
  LOG(INFO) << "Exec SQL: " << CreateTableStmt(test_any_table4);
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(test_any_table4), "Feature Not Supported", "");

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
  EXEC_INVALID_STMT_WITH_ERROR(CreateTableStmt(table3), "Duplicate Table",
      "Table already exists");

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

TEST_F(YbSqlKeyspace, TestCreateSystemTable) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  // Allow writes to system keyspace.
  client::FLAGS_yb_system_namespace_readonly = false;

  // Create system table.
  EXEC_VALID_STMT("create table system.t (c int primary key, v int);");

  // Insert into system table.
  EXEC_VALID_STMT("insert into system.t (c, v) values (1, 2);");

  // Select from system table.
  EXEC_VALID_STMT("select * from system.t where c = 1;");
  std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).int32_value(), 2);
}

TEST_F(YbSqlKeyspace, TestSqlSelectInvalidTable) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  const string select_stmt = "SELECT * FROM my_keyspace1.test_table WHERE h1 = 1 AND h2 = 'h1';";

  LOG(INFO) << "Exec SQL: " << select_stmt;
  EXEC_INVALID_STMT_WITH_ERROR(select_stmt, "Table Not Found", "");
}

} // namespace sql
} // namespace yb
