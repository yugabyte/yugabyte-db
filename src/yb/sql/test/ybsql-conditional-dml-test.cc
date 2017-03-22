//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

namespace yb {
namespace sql {

class YbSqlConditionalDml : public YbSqlTestBase {
 public:
  YbSqlConditionalDml() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlConditionalDml, TestSqlConditionalDml) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  // Create the table 1.
  const char *create_stmt =
      "CREATE TABLE t (h1 int, h2 varchar, "
                      "r1 int, r2 varchar, "
                      "c1 int, c2 varchar, "
                      "primary key((h1, h2), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  // Test parsing of conditional DML:

  // Test insert IF NOT EXISTS
  PARSE_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, c1, c2) "
                   "VALUES (1, 'a', 2, 'b', 3, 'c') IF NOT EXISTS OR c1 = 0;");

  // Test insert IF NOT EXISTS again
  PARSE_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, c1, c2) "
                   "VALUES (1, 'a', 2, 'b', 3, 'c') IF NOT EXISTS AND h1 = 1;");

  // Test insert IF EXISTS
  PARSE_VALID_STMT("INSERT INTO t (k) VALUES (1) IF NOT EXISTS;");

  // Test insert IF NOT EXISTS or a column condition
  PARSE_VALID_STMT("INSERT INTO t (k, c) VALUES (1, 2) IF NOT EXISTS OR c = 1" );

  // Test insert IF NOT EXISTS and two column condition
  PARSE_VALID_STMT("INSERT INTO t (k, c) VALUES (1, 2) IF NOT EXISTS OR c >= 0 and c <= 1" );

  // Test update IF EXISTS
  PARSE_VALID_STMT("UPDATE t set c = 1 WHERE k = 1 IF EXISTS" );

  // Test update IF NOT EXISTS
  PARSE_VALID_STMT("UPDATE t set c = 1 WHERE k = 1 IF NOT EXISTS" );

  // Test update IF EXISTS and a column condition
  PARSE_VALID_STMT("UPDATE t set c = 1 WHERE k = 1 IF EXISTS AND c = 1" );

  // Test delete IF EXISTS
  PARSE_VALID_STMT("DELETE FROM t WHERE k = 1 IF EXISTS" );

  // Test delete IF EXISTS and a column condition
  PARSE_VALID_STMT("DELETE FROM t WHERE k = 1 IF EXISTS AND c = 1" );

  // Test delete IF EXISTS and 2 column conditions with paranthesis
  PARSE_VALID_STMT("DELETE FROM t WHERE k = 1 IF EXISTS AND (c = 1 OR c = 2)" );
}

} // namespace sql
} // namespace yb
