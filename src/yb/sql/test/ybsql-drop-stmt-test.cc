//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

namespace yb {
namespace sql {

#define EXEC_INVALID_DROP_STMT(sql_stmt, expected_error)                                           \
do {                                                                                               \
  Status s = processor->Run(sql_stmt);                                                             \
  ASSERT_FALSE(s.ok());                                                                            \
  ASSERT_FALSE(s.ToString().find(expected_error) == string::npos);                                 \
} while (false);

class YbSqlDropTable : public YbSqlTestBase {
 public:
  YbSqlDropTable() : YbSqlTestBase() {
  }

  inline const string CqlError(int pos, string last = "") {
    return "SQL Error (1." + std::to_string(pos) + "): Invalid CQL Statement" + last;
  }
};

TEST_F(YbSqlDropTable, TestSqlDropTable) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  SqlProcessor *processor = GetSqlProcessor();

  const string create_stmt = "CREATE TABLE human_resource1(id int primary key, name varchar);";
  const string drop_stmt = "DROP TABLE human_resource1";
  const string drop_cond_stmt = "DROP TABLE IF EXISTS human_resource1";
  const string not_found_drop_error = "SQL Error (1.11): Table Not Found";

  // No tables exist at this point. Verify that this statement fails.
  EXEC_INVALID_DROP_STMT(drop_stmt, not_found_drop_error);

  // Although the table doesn't exist, a DROP TABLE IF EXISTS should succeed.
  EXEC_VALID_STMT(drop_cond_stmt);

  // Now create the table.
  EXEC_VALID_STMT(create_stmt);

  // Now verify that we can drop the table.
  EXEC_VALID_STMT(drop_stmt);

  // Verify that the table was indeed deleted.
  EXEC_INVALID_DROP_STMT(drop_stmt, not_found_drop_error);

  // Create the table again.
  EXEC_VALID_STMT(create_stmt);

  // Now verify that we can drop the table with a DROP TABLE IF EXISTS statement.
  EXEC_VALID_STMT(drop_cond_stmt);

  // Verify that the table was indeed deleted.
  EXEC_INVALID_DROP_STMT(drop_stmt, not_found_drop_error);
}

TEST_F(YbSqlDropTable, TestSqlDropStmtParser) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  SqlProcessor *processor = GetSqlProcessor();

  vector<string> objects = {
      "TYPE",
      "DOMAIN",
      "INDEX CONCURRENTLY",
  };
  for (const auto& object : objects) {
    const string expected_drop_error = CqlError(strlen("DROP "),
                                                " - DROP " + object + " statement not supported");
    auto drop_stmt = "DROP " + object + " name";
    EXEC_INVALID_DROP_STMT(drop_stmt, expected_drop_error);

    const string expected_drop_if_exists_error =
        CqlError(strlen("DROP "), " - DROP " + object + " IF EXISTS statement not supported");
    auto drop_if_exists_stmt = "DROP " + object + " IF EXISTS name";
    EXEC_INVALID_DROP_STMT(drop_if_exists_stmt, expected_drop_if_exists_error);
  }

  vector<string> drop_types = {
      "SEQUENCE",
      "VIEW",
      "MATERIALIZED VIEW",
      "INDEX",
      "FOREIGN TABLE",
      "EVENT TRIGGER",
      "COLLATION",
      "CONVERSION",
      "SCHEMA",
      "EXTENSION",
      "TEXT SEARCH PARSER",
      "TEXT SEARCH DICTIONARY",
      "TEXT SEARCH TEMPLATE",
      "TEXT SEARCH CONFIGURATION"
  };
  for (const auto& drop_type : drop_types) {
    auto sql_stmt = "DROP " + drop_type + " name";
    EXEC_INVALID_DROP_STMT(sql_stmt, CqlError(strlen("DROP ")));
  }

  vector<string> opt_drop_behaviors = {"CASCADE", "RESTRICT"};
  for (const auto& opt_drop_behavior : opt_drop_behaviors) {
    auto sql_stmt = "DROP TABLE name ";
    EXEC_INVALID_DROP_STMT(sql_stmt + opt_drop_behavior, CqlError(strlen(sql_stmt)));
  }
}

TEST_F(YbSqlDropTable, TestSqlDropStmtAnalyzer) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  SqlProcessor *processor = GetSqlProcessor();

  string expected_drop_error = CqlError(strlen("DROP TABLE "),
                                        " - Only one table name is allowed in a drop statement");
  EXEC_INVALID_DROP_STMT("DROP TABLE a, b", expected_drop_error);
  EXEC_INVALID_DROP_STMT("DROP TABLE a, b, c", expected_drop_error);
  EXEC_INVALID_DROP_STMT("DROP TABLE a, b, c, d", expected_drop_error);

  expected_drop_error = CqlError(strlen("DROP TABLE IF EXISTS "),
                                 " - Only one table name is allowed in a drop statement");
  EXEC_INVALID_DROP_STMT("DROP TABLE IF EXISTS a, b", expected_drop_error);
  EXEC_INVALID_DROP_STMT("DROP TABLE IF EXISTS a, b, c", expected_drop_error);
  EXEC_INVALID_DROP_STMT("DROP TABLE IF EXISTS a, b, c, d", expected_drop_error);
}

} // namespace sql
} // namespace yb
