//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <thread>
#include <cmath>

#include "yb/ql/test/ql-test-base.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using strings::Substitute;

namespace yb {
namespace ql {

class QLTestSelectedExpr : public QLTestBase {
 public:
  QLTestSelectedExpr() : QLTestBase() {
  }
};

TEST_F(QLTestSelectedExpr, TestQLSelectNumericExpr) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions.";

  // Create the table and insert some value.
  const char *create_stmt =
    "CREATE TABLE test_numeric_expr(h1 int primary key,"
    "                               v1 bigint, v2 int, v3 smallint, v4 tinyint,"
    "                               v5 float, v6 double);";
  CHECK_VALID_STMT(create_stmt);
  CHECK_VALID_STMT("INSERT INTO test_numeric_expr(h1, v1, v2, v3, v4, v5, v6)"
                   "  VALUES(1, 11, 12, 13, 14, 15, 16);");

  // Select TTL and WRITETIME.
  // - TTL and WRITETIME are not suppported for primary column.
  CHECK_INVALID_STMT("SELECT TTL(h1) FROM test_numeric_expr WHERE h1 = 1;");
  CHECK_INVALID_STMT("SELECT WRITETIME(h1) FROM test_numeric_expr WHERE h1 = 1;");

  // Test various select.
  std::shared_ptr<QLRowBlock> row_block;

  // Select '*'.
  CHECK_VALID_STMT("SELECT * FROM test_numeric_expr WHERE h1 = 1;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& star_row = row_block->row(0);
  CHECK_EQ(star_row.column(0).int32_value(), 1);
  CHECK_EQ(star_row.column(1).int64_value(), 11);
  CHECK_EQ(star_row.column(2).int32_value(), 12);
  CHECK_EQ(star_row.column(3).int16_value(), 13);
  CHECK_EQ(star_row.column(4).int8_value(), 14);
  CHECK_EQ(star_row.column(5).float_value(), 15);
  CHECK_EQ(star_row.column(6).double_value(), 16);

  // Select expressions.
  CHECK_VALID_STMT("SELECT h1, v1+1, v2+2, v3+3, v4+4, v5+5, v6+6 "
                   "FROM test_numeric_expr WHERE h1 = 1;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& expr_row = row_block->row(0);
  CHECK_EQ(expr_row.column(0).int32_value(), 1);
  CHECK_EQ(expr_row.column(1).int64_value(), 12);
  CHECK_EQ(expr_row.column(2).int64_value(), 14);
  CHECK_EQ(expr_row.column(3).int64_value(), 16);
  CHECK_EQ(expr_row.column(4).int64_value(), 18);
  CHECK_EQ(expr_row.column(5).double_value(), 20);
  CHECK_EQ(expr_row.column(6).double_value(), 22);

  // Select with alias.
  CHECK_VALID_STMT("SELECT v1+1 as one, TTL(v2) as two FROM test_numeric_expr WHERE h1 = 1;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& expr_alias_row = row_block->row(0);
  CHECK_EQ(expr_alias_row.column(0).int64_value(), 12);
  CHECK(expr_alias_row.column(1).IsNull());
}

} // namespace ql
} // namespace yb
