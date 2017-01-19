//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using strings::Substitute;

namespace yb {
namespace sql {

class YbSqlQuery : public YbSqlTestBase {
 public:
  YbSqlQuery() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlQuery, TestSqlQuerySimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  SqlProcessor *processor = GetSqlProcessor();

  // Create the table.
  const char *create_stmt = "CREATE TABLE test_table"
    "(h1 smallint, h2 int, h3 bigint, h4 varchar,"
    " r1 smallint, r2 int, r3 bigint, r4 varchar,"
    " v1 smallint, v2 int, v3 bigint, v4 varchar, v5 float, v6 double precision, v7 boolean, "
    " primary key((h1, h2, h3, h4), r1, r2, r3, r4));";
  CHECK_VALID_STMT(create_stmt);

  // Test NOTFOUND. Select from empty table for all types.
  CHECK_INVALID_STMT("SELECT * FROM test_table");
  CHECK_VALID_STMT("SELECT * FROM test_table"
                   "  WHERE h1 = 0 AND h2 = 0 AND h3 = 0 AND h4 = 'zero';");
  std::shared_ptr<YSQLRowBlock> empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);

  // Insert 10 rows into the table.
  string stmt;
  static const int kNumRows = 10;
  for (int idx = 0; idx < kNumRows; idx++) {
    const char *bool_value = idx % 2 == 0 ? "true" : "false";

    // INSERT: Valid statement with column list.
    stmt = Substitute(
      "INSERT INTO test_table"
      "(h1, h2, h3, h4,"
      " r1, r2, r3, r4,"
      " v1, v2, v3, v4, v5, v6, v7)"
      " VALUES($0, $0, $0, 'h$0',"
      "        $1, $1, $1, 'r$1',"
      "        $2, $2, $2, 'v$2', $2, $2, $3);",
      idx, idx+100, idx+200, bool_value);
    LOG(INFO) << "Executing " << stmt;
    CHECK_VALID_STMT(stmt);
  }
  LOG(INFO) << kNumRows << " rows inserted";

  // Test simple query and result.
  CHECK_VALID_STMT("SELECT * FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 7 AND h3 = 7 AND h4 = 'h7';");

  std::shared_ptr<YSQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YSQLRow& row = row_block->row(0);
  CHECK_EQ(row.column(0).int16_value(), 7);
  CHECK_EQ(row.column(1).int32_value(), 7);
  CHECK_EQ(row.column(2).int64_value(), 7);
  CHECK_EQ(row.column(3).string_value(), "h7");
  CHECK_EQ(row.column(4).int16_value(), 107);
  CHECK_EQ(row.column(5).int32_value(), 107);
  CHECK_EQ(row.column(6).int64_value(), 107);
  CHECK_EQ(row.column(7).string_value(), "r107");
  CHECK_EQ(row.column(8).int16_value(), 207);
  CHECK_EQ(row.column(9).int32_value(), 207);
  CHECK_EQ(row.column(10).int64_value(), 207);
  CHECK_EQ(row.column(11).string_value(), "v207");
  int ival = row.column(12).float_value();
  CHECK(ival >= 206 && ival <= 207);
  ival = row.column(13).double_value();;
  CHECK(ival >= 206 && ival <= 207);
  CHECK_EQ(row.column(14).bool_value(), false);
}

} // namespace sql
} // namespace yb
