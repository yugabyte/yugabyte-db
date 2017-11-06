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

#include "yb/ql/test/ql-test-base.h"

namespace yb {
namespace ql {

using std::make_shared;
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

  // Valid statement: INSERT with multiple TTL and TIMESTAMP.
  PARSE_VALID_STMT("INSERT INTO human_resource (id, name) values (1, \"Joe\") USING TTL 1 AND "
                       "TIMESTAMP 100 AND TTL 100 AND TIMESTAMP 200");

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

  // Invalid statement: INSERT with unterminated quoted string literal.
  PARSE_INVALID_STMT("INSERT INTO human_resource VALUES(7, 'Scott Tiger;");

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

  // Valid statement: SELECT statement with different two-char operators and "?".
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1>=? AND C2<=? AND C3<>? AND C4!=?;");

  // Valid statement: SELECT statement with "=" and ":id" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=:c1;");

  // Valid statement: SELECT statement with "=" and ":number" with no space in between.
  PARSE_VALID_STMT("SELECT * FROM t WHERE C1=:1;");

  // Invalid statement: CREATE TABLE with "?" bind marker.
  PARSE_INVALID_STMT("CREATE TABLE t (c int PRIMARY KEY) WITH default_time_to_live = ?;");

  // Invalid statement: CREATE with invalid prop value.
  PARSE_INVALID_STMT("CREATE TABLE human_resource"
      "  (id int, name varchar, salary int, PRIMARY KEY ((id, name), salary)) WITH "
      "default_time_to_live = (1000 + 1000);");

  // Valid statement: CREATE table with inet type.
  PARSE_VALID_STMT("CREATE TABLE human_resource (c1 inet, c2 int, c3 int, PRIMARY KEY(c1));");
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
  // Valid statement: CREATE INDEX WITH CLUSTERING ORDER BY and COVERING.
  PARSE_VALID_STMT("CREATE INDEX IF NOT EXISTS i ON k.t ((c1, c2), c3, c4) "
                   "WITH CLUSTERING ORDER BY (c3 DESC, c4 ASC) COVERING (c5, c6);");

  // Invalid statement: mandatory index name missing.
  PARSE_INVALID_STMT("CREATE INDEX ON k.t (c1, c2, c3, c4);");
  // Invalid statement: index name must be simple name without keyspace qualifier.
  PARSE_INVALID_STMT("CREATE INDEX k.i ON k.t (c1, c2, c3, c4);");
}

}  // namespace ql
}  // namespace yb
