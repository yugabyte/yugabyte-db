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
  const char *sql_stmt1 = "CREATE TABLE human_resource1(id int, name varchar);";
  Status s = processor->Run(sql_stmt1);
  CHECK(s.ok());

  // Create the table 2. Use "id" as primary key.
  const char *sql_stmt2 = "CREATE TABLE human_resource2(id int primary key, name varchar);";
  s = processor->Run(sql_stmt2);
  CHECK(s.ok());

  // Create the table 3. Use "name" as primary key.
  const char *sql_stmt3 = "CREATE TABLE human_resource3(id int, name varchar primary key);";
  s = processor->Run(sql_stmt3);
  CHECK(s.ok());

  // Create the table 4. Use both "id" and "name" as primary key.
  const char *sql_stmt4 =
    "CREATE TABLE human_resource4(id int, name varchar, primary key(id, name));";
  s = processor->Run(sql_stmt4);
  CHECK(s.ok());

  const char *sql_stmt5 =
    "CREATE TABLE human_resource5(id int, name varchar, primary key((id), name));";
  s = processor->Run(sql_stmt5);
  CHECK(s.ok());

  const char *sql_stmt6 =
    "CREATE TABLE human_resource6 "
    "  (id int, name varchar, salary int, primary key((id, name), salary));";
  s = processor->Run(sql_stmt6);
  CHECK(s.ok());

  // Create the table 1 again. This must fail.
  s = processor->Run(sql_stmt1);
  CHECK(!s.ok());
}

} // namespace sql
} // namespace yb
