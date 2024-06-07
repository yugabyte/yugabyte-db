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

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;
using std::vector;

namespace yb {
namespace ql {

class TestQLDropStmt : public QLTestBase {
 public:
  TestQLDropStmt() : QLTestBase() {
  }

  inline const string CqlError(string last = "") {
    return "Invalid CQL Statement" + last;
  }
};

TEST_F(TestQLDropStmt, TestQLDropTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string create_stmt = "CREATE TABLE human_resource1(id int primary key, name varchar);";
  const string drop_stmt = "DROP TABLE human_resource1";
  const string drop_cond_stmt = "DROP TABLE IF EXISTS human_resource1";
  const string not_found_drop_error = "Object Not Found";

  // No tables exist at this point. Verify that this statement fails.
  EXEC_INVALID_STMT_WITH_ERROR(drop_stmt, not_found_drop_error);

  // Although the table doesn't exist, a DROP TABLE IF EXISTS should succeed.
  EXEC_VALID_STMT(drop_cond_stmt);

  // Now create the table.
  EXEC_VALID_STMT(create_stmt);

  // Now verify that we can drop the table.
  EXEC_VALID_STMT(drop_stmt);

  // Verify that the table was indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_stmt, not_found_drop_error);

  // Create the table again.
  EXEC_VALID_STMT(create_stmt);

  // Now verify that we can drop the table with a DROP TABLE IF EXISTS statement.
  EXEC_VALID_STMT(drop_cond_stmt);

  // Verify that the table was indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_stmt, not_found_drop_error);
}

TEST_F(TestQLDropStmt, TestQLDropIndex) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string create_table_stmt = "CREATE TABLE human_resource1"
                                   "(id int primary key, name varchar) "
                                   "with transactions = {'enabled':true};";
  const string create_index_stmt = "CREATE INDEX i ON human_resource1(name);";
  const string drop_table_stmt = "DROP TABLE human_resource1";
  const string drop_table_cond_stmt = "DROP TABLE IF EXISTS human_resource1";
  const string drop_index_stmt = "DROP INDEX i";
  const string drop_index_cond_stmt = "DROP INDEX IF EXISTS i";
  const string not_found_table_drop_error = "Object Not Found";
  const string not_found_index_drop_error = "Object Not Found";

  // No tables exist at this point. Verify that these statements fail.
  EXEC_INVALID_STMT_WITH_ERROR(drop_table_stmt, not_found_table_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR(drop_index_stmt, not_found_index_drop_error);

  // Although the table and the index doesn't exist, a DROP TABLE IF EXISTS should succeed.
  EXEC_VALID_STMT(drop_table_cond_stmt);
  EXEC_VALID_STMT(drop_index_cond_stmt);

  // Now create the table and the index.
  EXEC_VALID_STMT(create_table_stmt);
  EXEC_VALID_STMT(create_index_stmt);

  // Now verify that we can drop the index and the table.
  EXEC_VALID_STMT(drop_index_stmt);
  EXEC_VALID_STMT(drop_table_stmt);

  // Verify that the table and the index were indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_table_stmt, not_found_table_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR(drop_index_stmt, not_found_index_drop_error);

  // Create the table and index again.
  EXEC_VALID_STMT(create_table_stmt);
  EXEC_VALID_STMT(create_index_stmt);

  // Now verify that we can drop the table with a DROP TABLE IF EXISTS statement.
  EXEC_VALID_STMT(drop_index_cond_stmt);
  EXEC_VALID_STMT(drop_table_cond_stmt);

  // Verify that the table and the index were indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_table_stmt, not_found_table_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR(drop_index_stmt, not_found_index_drop_error);

  // Create the table and index again.
  EXEC_VALID_STMT(create_table_stmt);
  EXEC_VALID_STMT(create_index_stmt);

  // Now verify that we can drop the table only and the index will be dropped automatically.
  EXEC_VALID_STMT(drop_table_stmt);

  // Verify that the table and the index were indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_table_stmt, not_found_table_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR(drop_index_stmt, not_found_index_drop_error);
}

TEST_F(TestQLDropStmt, TestQLDropKeyspace) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  vector<string> objects = {"KEYSPACE", "SCHEMA"};
  for (const auto& object : objects) {
    const string create_keyspace = "CREATE " + object + " test;";
    const string drop_keyspace = "DROP " + object + " test;";
    const string drop_keyspace_cond = "DROP " + object + " IF EXISTS test;";
    const string use_keyspace = "USE test;";
    const string create_type = "CREATE TYPE employee (first_name text, last_name text, ssn int)";
    const string create_table = "CREATE TABLE test(id int primary key, v int);";
    const string drop_type = "DROP TYPE employee";
    const string drop_table = "DROP TABLE test";
    const string not_found_drop_error = "Keyspace Not Found";
    const string non_empty_drop_error = "Server Error";


    //----------------------------------------------------------------------------------------------
    // Test basic DROP KEYSPACE.
    //----------------------------------------------------------------------------------------------

    // No keyspaces exist at this point. Verify that this statement fails.
    EXEC_INVALID_STMT_WITH_ERROR(drop_keyspace, not_found_drop_error);

    // Although the keyspace doesn't exist, a DROP KEYSPACE IF EXISTS should succeed.
    EXEC_VALID_STMT(drop_keyspace_cond);

    // Now create the keyspace.
    EXEC_VALID_STMT(create_keyspace);

    // Now verify that we can drop the keyspace.
    EXEC_VALID_STMT(drop_keyspace);

    // Verify that the keyspace was indeed deleted.
    EXEC_INVALID_STMT_WITH_ERROR(drop_keyspace, not_found_drop_error);

    // Create the keyspace again.
    EXEC_VALID_STMT(create_keyspace);

    // Now verify that we can drop the keyspace with a DROP KEYSPACE IF EXISTS statement.
    EXEC_VALID_STMT(drop_keyspace_cond);

    // Verify that the keyspace was indeed deleted.
    EXEC_INVALID_STMT_WITH_ERROR(drop_keyspace, not_found_drop_error);

    //----------------------------------------------------------------------------------------------
    // Test DROP KEYSPACE for non-empty keyspace -- Containing a Table
    //----------------------------------------------------------------------------------------------

    // Create and Use the keyspace.
    EXEC_VALID_STMT(create_keyspace);
    EXEC_VALID_STMT(use_keyspace);

    // Create a type.
    EXEC_VALID_STMT(create_table);

    // Cannot drop keyspace with type inside.
    EXEC_INVALID_STMT_WITH_ERROR(drop_keyspace, non_empty_drop_error);

    // Create a type.
    EXEC_VALID_STMT(drop_table);

    // Now verify that we can drop the keyspace.
    EXEC_VALID_STMT(drop_keyspace);

    //----------------------------------------------------------------------------------------------
    // Test DROP KEYSPACE for non-empty keyspace -- Containing a (User-defined) Type
    //----------------------------------------------------------------------------------------------

    // Create and Use the keyspace.
    EXEC_VALID_STMT(create_keyspace);
    EXEC_VALID_STMT(use_keyspace);

    // Create a type.
    EXEC_VALID_STMT(create_type);

    // Cannot drop keyspace with type inside.
    EXEC_INVALID_STMT_WITH_ERROR(drop_keyspace, non_empty_drop_error);

    // Create a type.
    EXEC_VALID_STMT(drop_type);

    // Now verify that we can drop the keyspace.
    EXEC_VALID_STMT(drop_keyspace);

  }
}

TEST_F(TestQLDropStmt, TestQLDropType) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string create_type = "CREATE TYPE employee (first_name text, last_name text, ssn int)";
  const string create_table = "CREATE TABLE test(id int primary key, emp employee);";
  const string drop_type = "DROP TYPE employee";
  const string drop_table = "DROP TABLE test";
  const string drop_type_cond = "DROP TYPE IF EXISTS employee";
  const string not_found_drop_error = "Type Not Found";
  const string type_in_use_error = "Invalid Request";

  //------------------------------------------------------------------------------------------------
  // Test basic DROP TYPE.
  //------------------------------------------------------------------------------------------------

  // No types exist at this point. Verify that this statement fails.
  EXEC_INVALID_STMT_WITH_ERROR(drop_type, not_found_drop_error);

  // Create the type and a table using it.
  EXEC_VALID_STMT(create_type);
  EXEC_VALID_STMT(create_table);

  // Verify that we cannot drop the type (because the table is referencing it).
  EXEC_INVALID_STMT_WITH_ERROR(drop_type, type_in_use_error);

  // Drop the table and verify we can now drop the type;
  EXEC_VALID_STMT(drop_table);
  EXEC_VALID_STMT(drop_type);

  // Verify that the type was indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_type, not_found_drop_error);

  //------------------------------------------------------------------------------------------------
  // Test DROP TYPE with condition (IF NOT EXISTS)
  //------------------------------------------------------------------------------------------------

  // Although the type doesn't exist, a DROP TABLE IF EXISTS should succeed.
  EXEC_VALID_STMT(drop_type_cond);

  // Create the type and table again.
  EXEC_VALID_STMT(create_type);
  EXEC_VALID_STMT(create_table);

  // Verify that DROP TYPE IF EXISTS fails because table is referencing type.
  EXEC_INVALID_STMT_WITH_ERROR(drop_type, type_in_use_error);

  // Drop the table and verify we can now drop the type;
  EXEC_VALID_STMT(drop_table);
  EXEC_VALID_STMT(drop_type_cond);

  // Verify that the type was indeed deleted.
  EXEC_INVALID_STMT_WITH_ERROR(drop_type, not_found_drop_error);
}

TEST_F(TestQLDropStmt, TestQLDropStmtParser) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  vector<string> objects = {
      "DOMAIN",
      "INDEX CONCURRENTLY",
  };
  for (const auto& object : objects) {
    const string expected_drop_error = CqlError(". DROP " + object + " statement not supported");
    auto drop_stmt = "DROP " + object + " test";
    EXEC_INVALID_STMT_WITH_ERROR(drop_stmt, expected_drop_error);

    const string expected_drop_if_exists_error =
        CqlError(". DROP " + object + " IF EXISTS statement not supported");
    auto drop_if_exists_stmt = "DROP " + object + " IF EXISTS test";
    EXEC_INVALID_STMT_WITH_ERROR(drop_if_exists_stmt, expected_drop_if_exists_error);
  }

  vector<string> drop_types = {
      "SEQUENCE",
      "VIEW",
      "MATERIALIZED VIEW",
      "FOREIGN TABLE",
      "EVENT TRIGGER",
      "COLLATION",
      "CONVERSION",
      "EXTENSION",
      "TEXT SEARCH PARSER",
      "TEXT SEARCH DICTIONARY",
      "TEXT SEARCH TEMPLATE",
      "TEXT SEARCH CONFIGURATION"
  };
  for (const auto& drop_type : drop_types) {
    auto stmt = "DROP " + drop_type + " test";
    EXEC_INVALID_STMT_WITH_ERROR(stmt, CqlError());
  }

  vector<string> opt_drop_behaviors = {"CASCADE", "RESTRICT"};
  for (const auto& opt_drop_behavior : opt_drop_behaviors) {
    auto stmt = "DROP TABLE test ";
    EXEC_INVALID_STMT_WITH_ERROR(stmt + opt_drop_behavior, CqlError());
  }
}

TEST_F(TestQLDropStmt, TestQLDropStmtAnalyzer) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  string expected_drop_error = CqlError(". Only one object name is allowed in a drop statement");
  EXEC_INVALID_STMT_WITH_ERROR("DROP TABLE a, b", expected_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR("DROP TABLE a, b, c", expected_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR("DROP TABLE a, b, c, d", expected_drop_error);

  expected_drop_error = CqlError(". Only one object name is allowed in a drop statement");
  EXEC_INVALID_STMT_WITH_ERROR("DROP TABLE IF EXISTS a, b", expected_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR("DROP TABLE IF EXISTS a, b, c", expected_drop_error);
  EXEC_INVALID_STMT_WITH_ERROR("DROP TABLE IF EXISTS a, b, c, d", expected_drop_error);
}

} // namespace ql
} // namespace yb
