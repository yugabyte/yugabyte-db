//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

#define EXEC_INVALID_STMT_WITH_ERROR(sql_stmt, expected_error, expected_error_msg)                 \
do {                                                                                               \
  Status s = processor->Run(sql_stmt);                                                             \
  EXPECT_FALSE(s.ok());                                                                            \
  if (!std::string(expected_error).empty()) {                                                      \
    EXPECT_FALSE(s.ToString().find(expected_error) == string::npos);                               \
  }                                                                                                \
  if (!std::string(expected_error_msg).empty()) {                                                  \
    EXPECT_FALSE(s.ToString().find(expected_error_msg) == string::npos);                           \
  }                                                                                                \
} while (false);

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
};

TEST_F(YbSqlKeyspace, TestSqlCreateKeyspaceSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  SqlProcessor *processor = GetSqlProcessor();

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
  SqlProcessor *processor = GetSqlProcessor();

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
  SqlProcessor *processor = GetSqlProcessor();

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
  SqlProcessor *processor = GetSqlProcessor();

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
  SqlProcessor *processor = GetSqlProcessor();

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

} // namespace sql
} // namespace yb
