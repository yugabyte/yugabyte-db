//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <thread>
#include <cmath>

#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/gutil/strings/substitute.h"

DECLARE_bool(test_tserver_timeout);

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

TEST_F(QLTestSelectedExpr, TestAggregateExpr) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions.";

  // Create the table and insert some value.
  const char *create_stmt =
    "CREATE TABLE test_aggr_expr(h int, r int,"
    "                            v1 bigint, v2 int, v3 smallint, v4 tinyint,"
    "                            v5 float, v6 double, primary key(h, r));";
  CHECK_VALID_STMT(create_stmt);

  // Insert a rows whose hash value is '1'.
  CHECK_VALID_STMT("INSERT INTO test_aggr_expr(h, r, v1, v2, v3, v4, v5, v6)"
                   "  VALUES(1, 777, 11, 12, 13, 14, 15, 16);");

  // Insert the rest of the rows, one of which has hash value of '1'.
  int64_t v1_total = 11;
  int32_t v2_total = 12;
  int16_t v3_total = 13;
  int8_t v4_total = 14;
  float v5_total = 15;
  double v6_total = 16;
  for (int i = 1; i < 20; i++) {
    string stmt = strings::Substitute(
        "INSERT INTO test_aggr_expr(h, r, v1, v2, v3, v4, v5, v6)"
        "  VALUES($0, $1, $2, $3, $4, $5, $6, $7);",
        i, i + 1, i + 1000, i + 100, i + 10, i, i + 77.77, i + 999.99);
    CHECK_VALID_STMT(stmt);

    v1_total += (i + 1000);
    v2_total += (i + 100);
    v3_total += (i + 10);
    v4_total += i;
    v5_total += (i + 77.77);
    v6_total += (i + 999.99);
  }

  std::shared_ptr<QLRowBlock> row_block;

  //------------------------------------------------------------------------------------------------
  // Test COUNT() aggregate function.
  {
    // Test COUNT() - Not existing data.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int64_value(), 0);

    // Test COUNT() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(1).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(2).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(3).int64_value(), 1);

    // Test COUNT() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(1).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(2).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(3).int64_value(), 2);

    // Test COUNT() - All rows.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(1).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(2).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(3).int64_value(), 20);
  }

  //------------------------------------------------------------------------------------------------
  // Test SUM() aggregate function.
  {
    // Test SUM() - Not existing data.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int32_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int16_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int8_value(), 0);
    CHECK_EQ(sum_0_row.column(4).float_value(), 0);
    CHECK_EQ(sum_0_row.column(5).double_value(), 0);

    // Test SUM() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_1_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test SUM() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1012);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 113);
    CHECK_EQ(sum_2_row.column(2).int16_value(), 24);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 15);
    // Comparing floating point for 93.77
    CHECK_GT(sum_2_row.column(4).float_value(), 93.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 93.775);
    // Comparing floating point for 1016.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1016.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1016.995);

    // Test SUM() - All rows.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), v1_total);
    CHECK_EQ(sum_all_row.column(1).int32_value(), v2_total);
    CHECK_EQ(sum_all_row.column(2).int16_value(), v3_total);
    CHECK_EQ(sum_all_row.column(3).int8_value(), v4_total);
    CHECK_GT(sum_all_row.column(4).float_value(), v5_total - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_total + 0.1);
    CHECK_GT(sum_all_row.column(5).double_value(), v6_total - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_total + 0.1);
  }

  //------------------------------------------------------------------------------------------------
  // Test MAX() aggregate functions.
  {
    // Test MAX() - Not exist.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());

    // Test MAX() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_1_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test MAX() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1001);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 101);
    CHECK_EQ(sum_2_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 14);
    // Comparing floating point for 78.77
    CHECK_GT(sum_2_row.column(4).float_value(), 78.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 78.775);
    // Comparing floating point for 1000.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1000.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1000.995);

    // Test MAX() - All rows.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 1019);
    CHECK_EQ(sum_all_row.column(1).int32_value(), 119);
    CHECK_EQ(sum_all_row.column(2).int16_value(), 29);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 19);
    float v5_max = 96.77;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_max - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_max + 0.1);
    double v6_max = 1018.99;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_max - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_max + 0.1);
  }

  //------------------------------------------------------------------------------------------------
  // Test MIN() aggregate functions.
  {
    // Test MIN() - Not exist.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());

    // Test MIN() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_1_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test MIN() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_2_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 1);
    // Comparing floating point for 15
    CHECK_GT(sum_2_row.column(4).float_value(), 14.9);
    CHECK_LT(sum_2_row.column(4).float_value(), 15.1);
    // Comparing floating point for 16
    CHECK_GT(sum_2_row.column(5).double_value(), 15.9);
    CHECK_LT(sum_2_row.column(5).double_value(), 16.1);

    // Test MIN() - All rows.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_all_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_all_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 1);
    float v5_min = 15;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_min - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_min + 0.1);
    double v6_min = 16;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_min - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_min + 0.1);
  }
}

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

TEST_F(QLTestSelectedExpr, TestQLSelectToken) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions.";

  // Create the table and insert some value.
  const char *create_stmt =
      "CREATE TABLE test_select_token(h1 int, h2 double, h3 text, "
      "                               r int, v int, primary key ((h1, h2, h3), r));";

  CHECK_VALID_STMT(create_stmt);

  CHECK_VALID_STMT("INSERT INTO test_select_token(h1, h2, h3, r, v) VALUES (1, 2.0, 'a', 1, 1)");
  CHECK_VALID_STMT("INSERT INTO test_select_token(h1, h2, h3, r, v) VALUES (11, 22.5, 'bc', 1, 1)");

  // Test various selects.
  std::shared_ptr<QLRowBlock> row_block;

  // Get the token for the first row.
  CHECK_VALID_STMT("SELECT token(h1, h2, h3) FROM test_select_token "
      "WHERE h1 = 1 AND h2 = 2.0 AND h3 = 'a';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  int64_t token1 = row_block->row(0).column(0).int64_value();

  // Check the token value matches the row.
  CHECK_VALID_STMT(Substitute("SELECT h1, h2, h3 FROM test_select_token "
      "WHERE token(h1, h2, h3) = $0", token1));
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& row1 = row_block->row(0);
  CHECK_EQ(row1.column(0).int32_value(), 1);
  CHECK_EQ(row1.column(1).double_value(), 2.0);
  CHECK_EQ(row1.column(2).string_value(), "a");

  // Get the token for the second row (also test additional selected columns).
  CHECK_VALID_STMT("SELECT v, token(h1, h2, h3), h3 FROM test_select_token "
      "WHERE h1 = 11 AND h2 = 22.5 AND h3 = 'bc';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  // Check the other selected columns return expected result.
  CHECK_EQ(row_block->row(0).column(0).int32_value(), 1);
  CHECK_EQ(row_block->row(0).column(2).string_value(), "bc");
  int64_t token2 = row_block->row(0).column(1).int64_value();

  // Check the token value matches the row.
  CHECK_VALID_STMT(Substitute("SELECT h1, h2, h3 FROM test_select_token "
      "WHERE token(h1, h2, h3) = $0", token2));
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& row2 = row_block->row(0);
  CHECK_EQ(row2.column(0).int32_value(), 11);
  CHECK_EQ(row2.column(1).double_value(), 22.5);
  CHECK_EQ(row2.column(2).string_value(), "bc");
}


TEST_F(QLTestSelectedExpr, TestTserverTimeout) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  const char *create_stmt = "CREATE TABLE test_table(h int, primary key(h));";
  CHECK_VALID_STMT(create_stmt);
  // Insert a row whose hash value is '1'.
  CHECK_VALID_STMT("INSERT INTO test_table(h) VALUES(1);");
  // Make sure a select statement works.
  CHECK_VALID_STMT("SELECT count(*) FROM test_table WHERE h = 1;");
  // Set a flag to simulate tserver timeout and now check that select produces an error.
  FLAGS_test_tserver_timeout = true;
  CHECK_INVALID_STMT("SELECT count(*) FROM test_table WHERE h = 1;");
}

} // namespace ql
} // namespace yb
