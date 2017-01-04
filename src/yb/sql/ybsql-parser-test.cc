//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"

namespace yb {
namespace sql {

using std::make_shared;
using std::string;

class YbSqlTestParser : public YbSqlTestBase {
 public:
  YbSqlTestParser() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlTestParser, TestSqlParser) {

#define EXEC_VALID_STMT(sql_stmt) \
do { \
  ErrorCode errcode = TestParser(sql_stmt); \
  ASSERT_EQ(errcode, ErrorCode::SUCCESSFUL_COMPLETION); \
} while (false)

#define EXEC_INVALID_STMT(sql_stmt) \
do { \
  ErrorCode errcode = TestParser(sql_stmt); \
  ASSERT_NE(errcode, ErrorCode::SUCCESSFUL_COMPLETION); \
} while (false)

  // Valid statement: Simple CREATE.
  EXEC_VALID_STMT("CREATE TABLE human_resource(id int, name varchar);");

  // Valid statement: CREATE with PRIMARY KEY
  EXEC_VALID_STMT("CREATE TABLE human_resource(id int PRIMARY KEY, name varchar);");

  // Valid statement: CREATE with PRIMARY KEY
  EXEC_VALID_STMT("CREATE TABLE human_resource"
                  "  (id int, name varchar, salary int, PRIMARY KEY (id, name));");

  // Valid statement: CREATE with PRIMARY KEY
  EXEC_VALID_STMT("CREATE TABLE human_resource"
                  "  (id int, name varchar, salary int, PRIMARY KEY ((id), name));");

  // Valid statement: CREATE with PRIMARY KEY
  EXEC_VALID_STMT("CREATE TABLE human_resource"
                  "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary));");

  // Valid statement: INSERT.
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name) VALUES(7, \"Scott Tiger\");");

  // Valid statement: UPDATE.
  EXEC_VALID_STMT("UPDATE human_resource SET name = \"Joe Street\" WHERE id = 7;");

  // Valid statement: SELECT.
  EXEC_VALID_STMT("SELECT * FROM human_resource;");

  // Invalid statements. Currently, not all errors are reported, so this test list is limmited.
  // Currently, we're reporting some scanning errors with error code (1).
  EXEC_INVALID_STMT("CREATE TABLE human_resource(id int;");

  // Invalid statement: CREATE.
  EXEC_INVALID_STMT("CREATE TABLE human_resource(id int, name varchar(20));");

  // Invalid statement: INSERT.
  EXEC_INVALID_STMT("INSERT INTO human_resource VALUES(7, \"Scott Tiger\";");

  // Valid statement: SELECT.
  EXEC_VALID_STMT("SELECT id, name FROM human_resource;");
}

}  // namespace sql
}  // namespace yb
