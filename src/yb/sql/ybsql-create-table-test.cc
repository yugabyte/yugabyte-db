//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"

namespace yb {
namespace sql {

class YbSqlCreateTable : public YbSqlTestBase {
 public:
  YbSqlCreateTable() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlCreateTable, TestSqlCreateTableSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  SqlProcessor *processor = GetSqlProcessor();

  // Create the table 1.
  EXEC_VALID_STMT("CREATE TABLE human_resource1(id int, name varchar);");

  // Create the table 2. Use "id" as primary key.
  EXEC_VALID_STMT("CREATE TABLE human_resource2(id int primary key, name varchar);");

  // Create the table 3. Use "name" as primary key.
  EXEC_VALID_STMT("CREATE TABLE human_resource3(id int, name varchar primary key);");

  // Create the table 4. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT("CREATE TABLE human_resource4(id int, name varchar, primary key(id, name));");

  // Create the table 5. Use both "id" as hash primary key.
  EXEC_VALID_STMT("CREATE TABLE human_resource5(id int, name varchar, primary key((id), name));");

  // Create the table 4. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT("CREATE TABLE human_resource6 "
                  "  (id int, name varchar, salary int, primary key((id, name), salary));");;

  EXEC_INVALID_STMT("CREATE TABLE human_resource1(id int, name varchar);");
}

} // namespace sql
} // namespace yb
