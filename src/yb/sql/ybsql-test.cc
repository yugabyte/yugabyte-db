// Copyright (c) YugaByte, Inc.

#include <gtest/gtest.h>
#include <string>

#include "yb/sql/ybsql.h"
#include "yb/util/test_util.h"

namespace yb {
namespace sql {

using std::make_shared;
using std::string;

// Base class for all future SQL test cases. We are still prototyping, so I'll add some simple test
// cases here, but all of them should be organized into appropriate subclasses.
class YbSqlTestBase : public YBTest {
 public:
  YbSqlTestBase() :
    ybsql_(make_shared<YbSql>()) {
  }

  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();
  }

  virtual void TearDown() OVERRIDE {
    YBTest::TearDown();
  }

  // Access function to ybsql_;
  YbSql::SharedPtr ybsql() {
    return ybsql_;
  }

 private:
  YbSql::SharedPtr ybsql_;
};

TEST_F(YbSqlTestBase, TestSqlScanner) {
  string sql_stmt;
  int errcode;

  // Valid statement: CREATE.
  sql_stmt = "CREATE TABLE human_resource(id int, name varchar(20));";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, ERRCODE_SUCCESSFUL_COMPLETION);

  // Valid statement: INSERT.
  sql_stmt = "INSERT INTO human_resource VALUES(7, \"Scott Tiger\");";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, ERRCODE_SUCCESSFUL_COMPLETION);

  // Valid statement: UPDATE.
  sql_stmt = "UPDATE human_resource SET name = \"Joe Street\" WHERE id = 7;";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, ERRCODE_SUCCESSFUL_COMPLETION);

  // Valid statement: SELECT.
  sql_stmt = "SELECT * FROM human_resource;";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, ERRCODE_SUCCESSFUL_COMPLETION);

  // Invalid statements. Currently, not all errors are reported, so this test list is limmited.
  // Currently, we're reporting some scanning errors with error code (1).
  sql_stmt = "CREATE TABLE human_resource(id int;";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, 1);

  // Invalid statement: INSERT.
  sql_stmt = "INSERT INTO human_resource VALUES(7, \"Scott Tiger\";";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, 1);

  // Valid statement: SELECT.
  sql_stmt = "SELECT id, name FROM human_resource;";
  errcode = ybsql()->Process(sql_stmt);
  ASSERT_EQ(errcode, ERRCODE_SUCCESSFUL_COMPLETION);
}

}  // namespace sql.
}  // namespace yb.
