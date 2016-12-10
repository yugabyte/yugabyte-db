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

  // Create a session for this test.
  SessionContext *session_context = CreateSessionContext();

  // Create the table 1.
  const char *sql_stmt1 = "CREATE TABLE human_resource1(id int, name varchar);";
  ErrorCode errcode = ybsql_->Process(session_context, sql_stmt1);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // Create the table 2. Use "id" as primary key.
  const char *sql_stmt2 = "CREATE TABLE human_resource2(id int primary key, name varchar);";
  errcode = ybsql_->Process(session_context, sql_stmt2);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // Create the table 3. Use "name" as primary key.
  const char *sql_stmt3 = "CREATE TABLE human_resource3(id int, name varchar primary key);";
  errcode = ybsql_->Process(session_context, sql_stmt3);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // Create the table 4. Use both "id" and "name" as primary key.
  const char *sql_stmt4 =
    "CREATE TABLE human_resource4(id int, name varchar, primary key(id, name));";
  errcode = ybsql_->Process(session_context, sql_stmt4);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  const char *sql_stmt5 =
    "CREATE TABLE human_resource5(id int, name varchar, primary key((id), name));";
  errcode = ybsql_->Process(session_context, sql_stmt5);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  const char *sql_stmt6 =
    "CREATE TABLE human_resource6 "
    "  (id int, name varchar, salary int, primary key((id, name), salary));";
  errcode = ybsql_->Process(session_context, sql_stmt6);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // Create the table 1 again. This must fail.
  errcode = ybsql_->Process(session_context, sql_stmt1);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);
}

} // namespace sql
} // namespace yb
