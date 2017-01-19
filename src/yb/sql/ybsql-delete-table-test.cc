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

  // -----------------------------------------------------------------------------------------------
  // Create the table.
  const char *create_stmt =
    "CREATE TABLE test_table(h1 int, h2 varchar, "
                            "r1 int, r2 varchar, "
                            "v1 int, v2 varchar, "
                            "primary key((h1, h2), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  // -----------------------------------------------------------------------------------------------
  // Unknown table.
  CHECK_INVALID_STMT("DELETE from test_table_unknown WHERE h1 = 0 AND h2 = 'zero';");

  // Missing hash key.
  CHECK_INVALID_STMT("DELETE from test_table;");
  CHECK_INVALID_STMT("DELETE from test_table WHERE h1 = 0;");

  // Wrong operator on hash key.
  CHECK_INVALID_STMT("DELETE from test_table WHERE h1 > 0 AND h2 = 'zero';");

  // -----------------------------------------------------------------------------------------------
  // TESTCASE: DELETE statement using only hash key (partition key).
  // Insert 100 rows into the table.
  static const int kNumRows = 100;
  string select_stmt;

  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, 'h$1', $2, 'r$3', $4, 'v$5');",
                             idx, idx, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }

  // Testing DELETE one row.
  for (int idx = 0; idx < kNumRows; idx++) {
    // SELECT an entry to make sure it's there.
    select_stmt = Substitute("SELECT * FROM test_table"
                             "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 = $2 AND r2 = 'r$3';",
                             idx, idx, idx+100, idx+100);
    CHECK_VALID_STMT(select_stmt);
    std::shared_ptr<YSQLRowBlock> one_row_block = processor->row_block();
    CHECK_EQ(one_row_block->row_count(), 1);

    // DELETE the entry.
    CHECK_VALID_STMT(Substitute("DELETE FROM test_table"
                                "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 = $2 AND r2 = 'r$3';",
                                idx, idx, idx+100, idx+100));

    // SELECT the same entry to make sure it's no longer there.
    CHECK_VALID_STMT(select_stmt);
    std::shared_ptr<YSQLRowBlock> empty_row_block = processor->row_block();
    CHECK_EQ(empty_row_block->row_count(), 0);
  }

#if 0
  // -----------------------------------------------------------------------------------------------
  // TESTCASE: DELETE statement using range key.
  // Insert 100 rows into the table that share the same partition key.
  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2)"
                             "  VALUES($0, 'h$1', $2, 'r$3', $4, 'v$5');",
                             9999, 9999, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }
  LOG(INFO) << kNumRows << "rows were inserted";

  // Delete the first half of the table and check.
  // SELECT entries to make sure they are there.
  select_stmt = Substitute("SELECT * FROM test_table"
                           "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 < $2 AND r2 < 'r$3';",
                           9999, 9999, 150, 150);
  CHECK_VALID_STMT(select_stmt);
  std::shared_ptr<YSQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 50);
  LOG(INFO) << "50 rows were selected";

  // DELETE the entry.
  CHECK_VALID_STMT(Substitute("DELETE FROM test_table"
                              "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 < $2 AND r2 < 'r$3';",
                              9999, 9999, 150, 150));
  LOG(INFO) << "Expecting that 50 rows were deleted";

  // SELECT the same entries to make sure they are no longer there.
  CHECK_VALID_STMT(select_stmt);
  row_block = processor->row_block();
  if (row_block->row_count() > 0) {
    LOG(WARNING) << "Feature not yet supported. Not all rows are deleted";
  } else {
    LOG(INFO) << "50 rows were deleted";
  }

  // Delete the rest of the table and check.
  // SELECT entries to make sure they are there.
  select_stmt = Substitute("SELECT * FROM test_table"
                           "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 > $2 AND r2 > 'r$3';",
                           9999, 9999, 149, 149);
  CHECK_VALID_STMT(select_stmt);
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 50);
  LOG(INFO) << "50 rows were selected";

  // DELETE the entry.
  CHECK_VALID_STMT(Substitute("DELETE FROM test_table"
                              "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 > $2 AND r2 > 'r$3';",
                              9999, 9999, 149, 149));
  LOG(INFO) << "Expecting that 50 rows were deleted";

  // SELECT the same entries to make sure they are no longer there.
  CHECK_VALID_STMT(select_stmt);
  row_block = processor->row_block();
  if (row_block->row_count() > 0) {
    LOG(WARNING) << "Feature not yet supported. Not all rows are deleted";
  } else {
    LOG(INFO) << "50 rows were deleted";
  }
#endif
}

} // namespace sql
} // namespace yb
