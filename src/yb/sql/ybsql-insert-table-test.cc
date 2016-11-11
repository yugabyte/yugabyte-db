//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"

namespace yb {
namespace sql {

class YbSqlInsertTable : public YbSqlTestBase {
 public:
  YbSqlInsertTable() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlInsertTable, TestSqlInsertTableSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Create a session for this test.
  SessionContext *session_context = CreateNewConnectionContext();

  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE human_resource(id int, name varchar, salary int, primary key(id, name));";
  ErrorCode errcode = ybsql_->Process(session_context, create_stmt);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Valid statement without column list.
  const char *insert_stmt = "INSERT INTO human_resource VALUES(1, 'Scott Tiger', 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Valid statement with column list.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(2, 'Scott Tiger', 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Duplicate key. INSERT execution is not implemented yet, so we
  // ignore this error for now.
  insert_stmt = "INSERT INTO human_resource VALUES(2, 'Scott Tiger', 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Wrong table name.
  insert_stmt = "INSERT INTO human_resource_wrong VALUES(1, 'Scott Tiger', 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource VALUES(1, 'Scott Tiger');";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource VALUES(1, 'Scott Tiger', 100, 200);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(1);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource(id, name) VALUES(1, 'Scott Tiger', 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Missing primary key (id).
  insert_stmt = "INSERT INTO human_resource(name, salary) VALUES('Scott Tiger', 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // INSERT: Invalid statement - Missing primary key (name). This is successful because currently
  // we support only one primary key.
  insert_stmt = "INSERT INTO human_resource(id, salary) VALUES(2, 100);";
  errcode = ybsql_->Process(session_context, insert_stmt);
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION);

  // Because string and numeric datatypes are implicitly compatible, we cannot test datatype
  // mismatch yet. Once timestamp, boolean, ... types are introduced, type incompability should be
  // tested here also.
}

} // namespace sql
} // namespace yb
