//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"

namespace yb {
namespace sql {

#define EXEC_DUPLICATE_TABLE_CREATE_STMT(sql_stmt)                                                 \
do {                                                                                               \
  Status s = processor->Run(sql_stmt);                                                             \
  EXPECT_FALSE(s.ok());                                                                            \
  EXPECT_FALSE(s.ToString().find("Duplicate Table - Already present") == string::npos);            \
} while (false)

class YbSqlCreateTable : public YbSqlTestBase {
 public:
  YbSqlCreateTable() : YbSqlTestBase() {
  }

  inline const string CreateStmt(string params) {
    return "CREATE TABLE " + params;
  }

  inline const string CreateIfNotExistsStmt(string params) {
    return "CREATE TABLE IF NOT EXISTS " + params;
  }
};

TEST_F(YbSqlCreateTable, TestSqlCreateTableSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  SqlProcessor *processor = GetSqlProcessor();

  const string table1 = "human_resource1(id int, name varchar, primary key(id));";
  const string table2 = "human_resource2(id int primary key, name varchar);";
  const string table3 = "human_resource3(id int, name varchar primary key);";
  const string table4 = "human_resource4(id int, name varchar, primary key(id, name));";
  const string table5 = "human_resource5(id int, name varchar, primary key((id), name));";
  const string table6 =
      "human_resource6(id int, name varchar, salary int, primary key((id, name), salary));";

  const string table7 = "human_resource7(id int, name varchar, primary key(id));";
  const string table8 = "human_resource8(id int primary key, name varchar);";
  const string table9 = "human_resource9(id int, name varchar primary key);";
  const string table10 = "human_resource10(id int, name varchar, primary key(id, name));";
  const string table11 = "human_resource11(id int, name varchar, primary key((id), name));";
  const string table12 =
      "human_resource12(id int, name varchar, salary int, primary key((id, name), salary));";

  // Create the table 1.
  EXEC_VALID_STMT(CreateStmt(table1));

  // Create the table 2. Use "id" as primary key.
  EXEC_VALID_STMT(CreateStmt(table2));

  // Create the table 3. Use "name" as primary key.
  EXEC_VALID_STMT(CreateStmt(table3));

  // Create the table 4. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT(CreateStmt(table4));

  // Create the table 5. Use both "id" as hash primary key.
  EXEC_VALID_STMT(CreateStmt(table5));

  // Create the table 6. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT(CreateStmt(table6));;

  // Create table 7.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table7));

  // Create the table 8. Use "id" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table8));

  // Create the table 9. Use "name" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table9));

  // Create the table 10. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table10));

  // Create the table 11. Use both "id" as hash primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table11));

  // Create the table 12. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table12));

  // Verify that all 'CREATE TABLE' statements fail for tables that have already been created.
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table1));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table2));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table3));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table4));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table5));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table6));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table7));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table8));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table9));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table10));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table11));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table12));

  // Verify that all 'CREATE TABLE IF EXISTS' statements succeed for tables that have already been
  // created.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table1));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table2));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table3));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table4));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table5));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table6));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table7));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table8));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table9));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table10));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table11));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table12));

  const string drop_stmt = "DROP TABLE human_resource1;";
  EXEC_VALID_STMT(drop_stmt);
}

} // namespace sql
} // namespace yb
