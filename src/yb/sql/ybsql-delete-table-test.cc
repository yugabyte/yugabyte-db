//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using strings::Substitute;

namespace yb {
namespace sql {

class YbSqlDeleteTable : public YbSqlTestBase {
 public:
  YbSqlDeleteTable() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlDeleteTable, TestSqlDeleteTableSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  SqlProcessor *processor = GetSqlProcessor();

  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE test_table(h1 int, h2 varchar, "
                            "r1 int, r2 varchar, "
                            "v1 int, v2 varchar, "
                            "primary key((h1, h2), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  // Insert 100 rows into the table.
  static const int kNumRows = 100;
  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, 'h$1', $2, 'r$3', $4, 'v$5');",
                             idx, idx, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt.c_str());
  }

#if 0
  // Testing DELETE.
  const char *update_stmt = nullptr;
#endif
}

} // namespace sql
} // namespace yb
