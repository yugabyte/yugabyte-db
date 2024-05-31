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

namespace yb {
namespace ql {

using std::string;

class QLTestParser : public QLTestBase {
 public:
  QLTestParser() : QLTestBase() {
  }
};

TEST_F(QLTestParser, TestQLParser) {

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

  // Invalid statement: CREATE with unterminated double quoted column.
  PARSE_INVALID_STMT("CREATE TABLE human_resource(\"id# int primary key, name varchar)");

  // Invalid statement: CREATE with tuple.
  // Tuples are not supported until #936
  PARSE_INVALID_STMT_ERR("CREATE TABLE human_resource(id int primary key, name tuple);",
                         "expecting '<'");
  PARSE_INVALID_STMT_ERR("CREATE TABLE human_resource(id int primary key, name tuple<>);",
                         "expecting '<'");
  PARSE_INVALID_STMT_ERR("CREATE TABLE human_resource(id int primary key, name tuple< >);",
                         "unexpected '>'");
  PARSE_INVALID_STMT_ERR("CREATE TABLE human_resource(id int primary key, name tuple<int>);",
                         "Feature Not Supported");
  PARSE_INVALID_STMT_ERR("CREATE TABLE human_resource(id int primary key, name tuple<int,int>);",
                         "Feature Not Supported");
  PARSE_INVALID_STMT_ERR("CREATE TABLE human_resource(id int primary key, name tuple<int,stuff>);",
                         "Feature Not Supported");

  // Valid statement: INSERT.
  PARSE_VALID_STMT("INSERT INTO human_resource(id, name) VALUES(7, \"Scott Tiger\");");

  // Valid statement: UPDATE.
  PARSE_VALID_STMT("UPDATE human_resource SET name = \"Joe Street\" WHERE id = 7;");

  // Valid statement: UPDATE with TTL.
  PARSE_VALID_STMT("UPDATE human_resource USING TTL 1 SET name = \"Joe Street\" WHERE id = 7;");

  // Valid statement: UPDATE with TTL and TIMESTAMP.
  PARSE_VALID_STMT("UPDATE human_resource USING TTL 1 AND TIMESTAMP 100 SET name = \"Joe Street\" "
                       "WHERE id = 7;");

  // Valid statement: UPDATE with TIMESTAMP.
  PARSE_VALID_STMT("UPDATE human_resource USING TIMESTAMP 100 SET name = \"Joe Street\" "
                       "WHERE id = 7;");

  // Valid statement: INSERT with TTL and TIMESTAMP.
  PARSE_VALID_STMT("INSERT INTO human_resource (id, name) values (1, \"Joe\") USING TTL 1 AND "
                       "TIMESTAMP 100");

  // Valid statement: INSERT with TTL.
  PARSE_VALID_STMT("INSERT INTO human_resource (id, name) values (1, \"Joe\") USING TTL 1");

  // Valid statement: INSERT with TIMESTAMP.
  PARSE_VALID_STMT("INSERT INTO human_resource (id, name) values (1, \"Joe\") USING TIMESTAMP 100");

  // Valid statement: INSERT with negative TIMESTAMP.
  PARSE_VALID_STMT("INSERT INTO human_resource (id, name) values (1, \"Joe\") USING TIMESTAMP "
                       "-100");

  // Valid statement: INSERT with multiple TTL and TIMESTAMP.
  PARSE_VALID_STMT("INSERT INTO human_resource (id, name) values (1, \"Joe\") USING TTL 1 AND "
                       "TIMESTAMP 100 AND TTL 100 AND TIMESTAMP 200");

  // Invalid statement: UPDATE with TTL.
  PARSE_INVALID_STMT("UPDATE human_resource USING 1 SET name = \"Joe Street\" WHERE id = 7;");

  // Valid statement: SELECT.
  PARSE_VALID_STMT("SELECT * FROM human_resource;");

  // Invalid statements. Currently, not all errors are reported, so this test list is limited.
  // Currently, we're reporting some scanning errors with error code (1).
  PARSE_INVALID_STMT("CREATE TABLE human_resource(id int;");

  // Invalid statement: CREATE.
  PARSE_INVALID_STMT("CREATE TABLE human_resource(id int, name varchar(20));");

  // Invalid statement: INSERT.
  PARSE_INVALID_STMT("INSERT INTO human_resource VALUES(7, \"Scott Tiger\";");

  // Invalid statement: INSERT with LIKE.
  PARSE_INVALID_STMT("INSERT INTO human_resource (id, name) values (1, 'Joe') IF id LIKE 4");
  PARSE_INVALID_STMT("INSERT INTO human_resource (id, name) values (1, 'Joe') IF id NOT LIKE 4");

  // Invalid statement: INSERT with unterminated quoted string literal.
  PARSE_INVALID_STMT("INSERT INTO human_resource VALUES(7, 'Scott Tiger;");

  // Invalid statement: INSERT/UPDATE/DELETE with declared but unsupported syntax.
  PARSE_INVALID_STMT("INSERT INTO human_resource DEFAULT VALUES 1");
  PARSE_INVALID_STMT("UPDATE human_resource SET salary = 1 WHERE id = 1 AND name= 'a' RETURNING 1");
  PARSE_INVALID_STMT("DELETE from human_resource WHERE id = 1 AND name = 'Joe' RETURNING 1");

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

  // Valid statement: SELECT statement with ":" named bind marker: quoted-identifier.
  PARSE_VALID_STMT("SELECT * FROM t WHERE c1 = :\"V1\" AND c2 = :\"V2\";");

  // Valid statement: SELECT statement with ":" named bind marker: type-name identifier.
  PARSE_VALID_STMT("SELECT * FROM t WHERE c1 = :timestamp AND c2 = :text;");
  PARSE_VALID_STMT("SELECT * FROM t WHERE c1 = :binary AND c2 = :int;");

  // Valid statement: INSERT statement with ":" number bind marker.
  PARSE_VALID_STMT("INSERT INTO t (c1, c2) VALUES (:1, :2);");

  // Valid statement: SELECT statement with "<unreserved-word>" bind marker.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1 = :KEY;");

  // Valid statement: SELECT statement with "=" and "?" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=?;");

  // Valid statement: SELECT statement with different two-char operators and "?".
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1>=? AND C2<=? AND C3<>? AND C4!=?;");

  // Valid statement: SELECT statement with "=" and ":id" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=:c1;");

  // Valid statement: SELECT statement with "=" and ":number" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=:1;");

  // Invalid statement: CREATE TABLE with "?" bind marker.
  PARSE_INVALID_STMT("CREATE TABLE t (c int PRIMARY KEY) WITH default_time_to_live = ?;");

  // Invalid statement: SELECT statement with ":" named bind marker: reserved keyword.
  PARSE_INVALID_STMT("SELECT * FROM t WHERE c1 = :true;");
  PARSE_INVALID_STMT("SELECT * FROM t WHERE c1 = :table;");

  // Invalid statement: SELECT with declared but unsupported syntax.
  PARSE_INVALID_STMT("SELECT * FROM t UNION SELECT * FROM t");
  PARSE_INVALID_STMT("TABLE 1");

  // Invalid statement: CREATE with invalid prop value.
  PARSE_INVALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = (1000 + 1000);");

  // Valid statement: CREATE table with inet type.
  PARSE_VALID_STMT("CREATE TABLE human_resource (c1 inet, c2 int, c3 int, PRIMARY KEY(c1));");

  // Create table with jsonb type.
  PARSE_VALID_STMT("CREATE TABLE human_resource (h1 int, r1 int, data jsonb, "
                       "PRIMARY KEY ((h1), r1));");
  // Valid json operators.
  PARSE_VALID_STMT("SELECT * FROM t WHERE c2->'a' = '1';");
  PARSE_VALID_STMT("SELECT * FROM t WHERE c2->'a'->'b' = '1';");
  PARSE_VALID_STMT("SELECT * FROM t WHERE c2->'a'->'b'->>'c' = '1';");
  PARSE_VALID_STMT("SELECT * FROM t WHERE c2->>'a' = '1';");
  PARSE_VALID_STMT("UPDATE t SET c2->'a' = '1' WHERE c1 = 1;");
  PARSE_VALID_STMT("UPDATE t SET c2->'a'->'b'->'c' = '1' WHERE c1 = 1;");
  PARSE_VALID_STMT("UPDATE t SET c2->'a'->'b'->'c' = '1', c3->'x'->'y'->'z' = '1' WHERE c1 = 1;");

  // Invalid json operators.
  PARSE_INVALID_STMT("SELECT * FROM t WHERE c2->>'a'->'b' = '1';");
  PARSE_INVALID_STMT("SELECT * FROM t WHERE c2->>a = '1';");
  PARSE_INVALID_STMT("SELECT * FROM t WHERE c2->a = '1';");
  PARSE_INVALID_STMT("SELECT c2->>'a'->'b' FROM t;");
  PARSE_INVALID_STMT("SELECT c2->>a  FROM t;");
  PARSE_INVALID_STMT("SELECT c2->a FROM t;");
  PARSE_INVALID_STMT("UPDATE t SET c2->>'a' = '1' WHERE c1 = 1;");

  // Valid statement: unreserved keywords used as names.
  PARSE_VALID_STMT("CREATE KEYSPACE clustering;");

  PARSE_VALID_STMT("CREATE TABLE off(filtering int PRIMARY KEY, login text, roles float)");

  // Invalid statement: ELSE ERROR
  PARSE_INVALID_STMT("DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'c' ELSE "
                     "ERROR;");
  PARSE_INVALID_STMT("INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c') "
                   "ELSE ERROR;");
  PARSE_INVALID_STMT("UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND "
                   "r2 = 'b' ELSE ERROR;");

  // Valid statement: ELSE ERROR
  PARSE_VALID_STMT("DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'c' IF EXISTS "
                   "ELSE ERROR;");
  PARSE_VALID_STMT("DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' IF EXISTS AND "
                   "v2 <> 'c' ELSE ERROR;");
  PARSE_VALID_STMT("DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' IF EXISTS AND "
                   "(v1 = 3 OR v1 = 4) ELSE ERROR;");
  PARSE_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c') "
                   "IF NOT EXISTS ELSE ERROR;");
  PARSE_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') "
                   "IF NOT EXISTS OR v1 <> 3 ELSE ERROR;");
  PARSE_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') "
                   "IF NOT EXISTS OR v1 < 3 ELSE ERROR;");
  PARSE_VALID_STMT("UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND "
                   "r2 = 'b' IF NOT EXISTS ELSE ERROR;");
  PARSE_VALID_STMT("UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND "
                   "r2 = 'b' IF NOT EXISTS OR v1 = 3 AND v2 = 'c' ELSE ERROR;");

  // Test the issue: https://github.com/yugabyte/yugabyte-db/issues/2476
  PARSE_INVALID_STMT("VALUES (1, 'Bob', 35, 'Ruby', '{\"name\": \"John\", 33, \"age\": 35}');");
}

TEST_F(QLTestParser, TestStaticColumn) {
  // Valid statement: CREATE TABLE with STATIC column.
  PARSE_VALID_STMT("CREATE TABLE human_resource"
                   "  (bldg_num int, room_num int, room_count int STATIC, sq_ft int, "
                   "PRIMARY KEY ((bldg_num), room_num));");

  // Valid statement: SELECT with DISTINCT.
  PARSE_VALID_STMT("SELECT DISTINCT h1, s1 FROM t;");

  // Invalid statement: wrong STATIC position.
  PARSE_INVALID_STMT("CREATE TABLE human_resource"
                     "  (bldg_num int, room_num int, room_count STATIC int, sq_ft int, "
                     "PRIMARY KEY ((bldg_num), room_num));");
}

TEST_F(QLTestParser, TestCreateIndex) {
  // Valid statement: CREATE INDEX.
  PARSE_VALID_STMT("CREATE INDEX i ON t (c1, c2, c3, c4);");
  // Valid statement: CREATE INDEX with keyspace-qualified indexed table.
  PARSE_VALID_STMT("CREATE INDEX i ON k.t ((c1, c2), c3, c4);");
  // Valid statement: CREATE INDEX IF NOT EXISTS.
  PARSE_VALID_STMT("CREATE INDEX IF NOT EXISTS i ON k.t ((c1, c2), c3, c4);");
  // Valid statement: CREATE INDEX WITH CLUSTERING ORDER BY
  PARSE_VALID_STMT("CREATE INDEX i ON k.t ((c1, c2), c3, c4) "
                   "WITH CLUSTERING ORDER BY (c3 DESC, c4 ASC);");
  // Valid statement: CREATE INDEX INCLUDE and WITH CLUSTERING ORDER BY.
  PARSE_VALID_STMT("CREATE INDEX IF NOT EXISTS i ON k.t ((c1, c2), c3, c4) "
                   "INCLUDE (c5, c6) WITH CLUSTERING ORDER BY (c3 DESC, c4 ASC);");
  // Valid statement: use COVERING in place of INCLUDE (allowed for compatibility with 1.1 beta).
  PARSE_VALID_STMT("CREATE INDEX IF NOT EXISTS i ON k.t ((c1, c2), c3, c4) "
                   "COVERING (c5, c6) WITH CLUSTERING ORDER BY (c3 DESC, c4 ASC);");

  // Valid statement: CREATE INDEX i ON (<json_attribute>).
  PARSE_VALID_STMT("CREATE INDEX i ON t (c2->>'a');");
  PARSE_VALID_STMT("CREATE INDEX i ON t (c2->'a'->>'b');");
  PARSE_VALID_STMT("CREATE INDEX i ON t (c2->'a'->'b'->>'c');");

  // Default index name.
  PARSE_VALID_STMT("CREATE INDEX ON k.t (c1, c2, c3, c4);");

  // Invalid statement: index name must be simple name without keyspace qualifier.
  PARSE_INVALID_STMT("CREATE INDEX k.i ON k.t (c1, c2, c3, c4);");
}

TEST_F(QLTestParser, TestTruncate) {
  // Valid statement: TRUNCATE [TABLE] [keyspace.]table.
  PARSE_VALID_STMT("TRUNCATE t;");
  PARSE_VALID_STMT("TRUNCATE TABLE t;");
  PARSE_VALID_STMT("TRUNCATE TABLE k.t;");

  // Invalid statement: invalid target type.
  PARSE_INVALID_STMT("TRUNCATE KEYSPACE k;");
  PARSE_INVALID_STMT("TRUNCATE TYPE t;");
}

TEST_F(QLTestParser, TestExplain) {
  // Valid statement: TRUNCATE [TABLE] [keyspace.]table.
  PARSE_VALID_STMT("EXPLAIN SELECT * FROM t WHERE C1=:c1;");
  PARSE_VALID_STMT("EXPLAIN INSERT INTO human_resource(id, name) VALUES(7, \"Scott Tiger\");");
  PARSE_VALID_STMT("EXPLAIN UPDATE human_resource SET name = \"Joe Street\" WHERE id = 7;");
  PARSE_VALID_STMT("EXPLAIN DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'c' "
                   "IF EXISTS ELSE ERROR;");
  // Invalid statement: invalid target type.
  PARSE_INVALID_STMT("EXPLAIN CREATE TABLE human_resource(id int, name varchar);");
  PARSE_INVALID_STMT("EXPLAIN TRUNCATE TABLE T;");
  PARSE_INVALID_STMT("EXPLAIN ANALYZE SELECT * FROM t WHERE C1=:c1;");
}

}  // namespace ql
}  // namespace yb
