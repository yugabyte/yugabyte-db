//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

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

  // Valid statement: Simple CREATE.
  PARSE_VALID_STMT("CREATE TABLE human_resource(id int, name varchar);");

  // Valid statement: CREATE with PRIMARY KEY
  PARSE_VALID_STMT("CREATE TABLE human_resource(id int PRIMARY KEY, name varchar);");

  // Valid statement: CREATE with PRIMARY KEY
  PARSE_VALID_STMT("CREATE TABLE human_resource"
                  "  (id int, name varchar, salary int, PRIMARY KEY (id, name));");

  // Valid statement: CREATE with PRIMARY KEY
  PARSE_VALID_STMT("CREATE TABLE human_resource"
                  "  (id int, name varchar, salary int, PRIMARY KEY ((id), name));");

  // Valid statement: CREATE with PRIMARY KEY
  PARSE_VALID_STMT("CREATE TABLE human_resource"
                  "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary));");

  // Valid statement: INSERT.
  PARSE_VALID_STMT("INSERT INTO human_resource(id, name) VALUES(7, \"Scott Tiger\");");

  // Valid statement: UPDATE.
  PARSE_VALID_STMT("UPDATE human_resource SET name = \"Joe Street\" WHERE id = 7;");

  // Valid statement: UPDATE with TTL.
  PARSE_VALID_STMT("UPDATE human_resource USING TTL 1 SET name = \"Joe Street\" WHERE id = 7;");

  // Invalid statement: UPDATE with TTL.
  PARSE_INVALID_STMT("UPDATE human_resource USING 1 SET name = \"Joe Street\" WHERE id = 7;");

  // Valid statement: SELECT.
  PARSE_VALID_STMT("SELECT * FROM human_resource;");

  // Invalid statements. Currently, not all errors are reported, so this test list is limmited.
  // Currently, we're reporting some scanning errors with error code (1).
  PARSE_INVALID_STMT("CREATE TABLE human_resource(id int;");

  // Invalid statement: CREATE.
  PARSE_INVALID_STMT("CREATE TABLE human_resource(id int, name varchar(20));");

  // Invalid statement: INSERT.
  PARSE_INVALID_STMT("INSERT INTO human_resource VALUES(7, \"Scott Tiger\";");

  // Valid statement: SELECT.
  PARSE_VALID_STMT("SELECT id, name FROM human_resource;");

  // Valid statement: CREATE with table properties.
  PARSE_VALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = 1000;");

  // Valid statement: CREATE with multiple table properties.
  // Note this is valid for the parser, but not the analyzer since duplicate properties are not
  // allowed. Once we support more than one property for the table, we can update this test.
  PARSE_VALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = 1000 AND default_time_to_live = 1000;");

  // Valid statement: CREATE with random property. This is an invalid statement, although the
  // parser doesn't fail on this, but the semantic phase catches this issue.
  PARSE_VALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "random_prop = 1000;");

  // Valid statement: SELECT statement with "?" bind marker.
  PARSE_VALID_STMT("SELECT * FROM t WHERE c1 = ? AND c2 = ?;");

  // Valid statement: SELECT statement with ":" named bind marker.
  PARSE_VALID_STMT("SELECT * FROM t WHERE c1 = :v1 AND c2 = :v2;");

  // Valid statement: SELECT statement with ":" named quoted-identifier bind marker.
  PARSE_VALID_STMT("SELECT * FROM t WHERE c1 = :\"V1\" AND c2 = :\"V2\";");

  // Valid statement: INSERT statement with ":" number bind marker.
  PARSE_VALID_STMT("INSERT INTO t (c1, c2) VALUES (:1, :2);");

  // Valid statement: SELECT statement with "<unreserved-word>" bind marker.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1 = :KEY;");

  // Valid statement: SELECT statement with "=" and "?" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=?;");

  // Valid statement: SELECT statement with "=" and ":id" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=:c1;");

  // Valid statement: SELECT statement with "=" and ":number" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=:1;");

  // Valid statement: SELECT statement with non-positive ":" bind position marker.
  PARSE_INVALID_STMT("SELECT * from t WHERE C1 = :0;");

  // Invalid statement: CREATE TABLE with "?" bind marker.
  PARSE_INVALID_STMT("CREATE TABLE t (c int PRIMARY KEY) WITH default_time_to_live = ?;");

  // Invalid statement: CREATE with invalid prop value.
  PARSE_INVALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = (1000 + 1000);");

  // Invalid statement: CREATE with table property value as string.
  PARSE_INVALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = 'abc';");

  // Invalid statement: CREATE with table property value as double.
  PARSE_INVALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = 1000.1;");

}

}  // namespace sql
}  // namespace yb
