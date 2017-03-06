//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include<thread>

#include "yb/sql/test/ybsql-test-base.h"
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

  std::shared_ptr<YQLRowBlock> ExecSelect(SqlProcessor *processor, int expected_rows = 1) {
    auto select = "SELECT c1, c2, c3 FROM test_table WHERE c1 = 1";
    Status s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(expected_rows, row_block->row_count());
    return row_block;
  }

  void VerifyExpiry(SqlProcessor *processor) {
    ExecSelect(processor, 0);
  }

  void CreateTableAndInsertRow(SqlProcessor *processor, bool with_ttl = true) {
    // Create the table.
    const char *create_stmt =
        "CREATE TABLE test_table(c1 int, c2 int, c3 int, "
            "primary key(c1));";
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());

    std::string insert_stmt("INSERT INTO test_table(c1, c2, c3) VALUES(1, 2, 3)");
    if (with_ttl) {
      // Insert row with ttl.
      insert_stmt += " USING TTL 1;";
    } else {
      insert_stmt += ";";
    }
    s = processor->Run(insert_stmt);
    CHECK_OK(s);

    // Verify row is present.
    auto row_block = ExecSelect(processor);
    YQLRow& row = row_block->row(0);

    EXPECT_EQ(1, row.column(0).int32_value());
    EXPECT_EQ(2, row.column(1).int32_value());
    EXPECT_EQ(3, row.column(2).int32_value());
  }

};

TEST_F(YbSqlQuery, TestSqlQuerySimple) {
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

  // Test NOTFOUND. Select from empty table.
  CHECK_INVALID_STMT("SELECT * FROM test_table");
  CHECK_VALID_STMT("SELECT * FROM test_table WHERE h1 = 0 AND h2 = ''");
  std::shared_ptr<YQLRowBlock> empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);

  // Insert 100 rows into the table.
  static const int kNumRows = 100;
  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, 'h$1', $2, 'r$3', $4, 'v$5');",
                             idx, idx, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }
  LOG(INFO) << kNumRows << " rows inserted";

  //------------------------------------------------------------------------------------------------
  // Basic negative cases.
  // Test simple query and result.
  CHECK_INVALID_STMT("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 = 7 AND h2 = 'h7' AND v1 = 1007;");
  CHECK_INVALID_STMT("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 = 7 AND h2 = 'h7' AND v1 = 100;");

  //------------------------------------------------------------------------------------------------
  // Test simple query and result.
  CHECK_VALID_STMT("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;");

  std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& ordered_row = row_block->row(0);
  CHECK_EQ(ordered_row.column(0).int32_value(), 7);
  CHECK_EQ(ordered_row.column(1).string_value(), "h7");
  CHECK_EQ(ordered_row.column(2).int32_value(), 107);
  CHECK_EQ(ordered_row.column(3).string_value(), "r107");
  CHECK_EQ(ordered_row.column(4).int32_value(), 1007);
  CHECK_EQ(ordered_row.column(5).string_value(), "v1007");

  // Test simple query and result with different order.
  CHECK_VALID_STMT("SELECT v1, v2, h1, h2, r1, r2 FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;");

  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& unordered_row = row_block->row(0);
  CHECK_EQ(unordered_row.column(0).int32_value(), 1007);
  CHECK_EQ(unordered_row.column(1).string_value(), "v1007");
  CHECK_EQ(unordered_row.column(2).int32_value(), 7);
  CHECK_EQ(unordered_row.column(3).string_value(), "h7");
  CHECK_EQ(unordered_row.column(4).int32_value(), 107);
  CHECK_EQ(unordered_row.column(5).string_value(), "r107");

  // Test single row query for the whole table.
  for (int idx = 0; idx < kNumRows; idx++) {
    // SELECT: Valid statement with column list.
    string stmt = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                             "WHERE h1 = $0 AND h2 = 'h$1' AND r1 = $2 AND r2 = 'r$3';",
                             idx, idx, idx+100, idx+100);
    CHECK_VALID_STMT(stmt);

    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const YQLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), idx);
    CHECK_EQ(row.column(1).string_value(), Substitute("h$0", idx));
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 1000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 1000));
  }

  // Test multi row query for the whole table.
  // Insert 20 rows of the same hash key into the table.
  static const int kHashNumRows = 20;
  int32 h1_shared = 1111111;
  const string h2_shared = "h2_shared_key";
  for (int idx = 0; idx < kHashNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, '$1', $2, 'r$3', $4, 'v$5');",
                             h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }

  // Select all 20 rows and check the values.
  const string multi_select = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                                         "WHERE h1 = $0 AND h2 = '$1';",
                                         h1_shared, h2_shared);
  CHECK_VALID_STMT(multi_select);
  row_block = processor->row_block();

  // Check the result set.
  CHECK_EQ(row_block->row_count(), kHashNumRows);
  for (int idx = 0; idx < kHashNumRows; idx++) {
    const YQLRow& row = row_block->row(idx);
    CHECK_EQ(row.column(0).int32_value(), h1_shared);
    CHECK_EQ(row.column(1).string_value(), h2_shared);
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 1000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 1000));
  }

  // Select all 2 rows and check the values.
  int limit = 2;
  const string limit_select = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                                           "WHERE h1 = $0 AND h2 = '$1' LIMIT $2;",
                                         h1_shared, h2_shared, limit);
  CHECK_VALID_STMT(limit_select);
  row_block = processor->row_block();

  // Check the result set.
  CHECK_EQ(row_block->row_count(), limit);
  int32_t prev_r1 = 0;
  string prev_r2;
  for (int idx = 0; idx < limit; idx++) {
    const YQLRow& row = row_block->row(idx);
    CHECK_EQ(row.column(0).int32_value(), h1_shared);
    CHECK_EQ(row.column(1).string_value(), h2_shared);
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 1000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 1000));
    CHECK_GT(row.column(2).int32_value(), prev_r1);
    CHECK_GT(row.column(3).string_value(), prev_r2);
    prev_r1 = row.column(2).int32_value();
    prev_r2 = row.column(3).string_value();
  }
}

TEST_F(YbSqlQuery, TestInsertWithTTL) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  SqlProcessor *processor = GetSqlProcessor();

  CreateTableAndInsertRow(processor);

  // Sleep for 1.1 seconds and verify ttl has expired.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  VerifyExpiry(processor);
}

TEST_F(YbSqlQuery, TestUpdateWithTTL) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  SqlProcessor *processor = GetSqlProcessor();

  CreateTableAndInsertRow(processor, false);

  // Now update the row with a TTL.
  std::string update_stmt("UPDATE test_table USING TTL 1 SET c2 = 4, c3 = 5 WHERE c1 = 1;");
  Status s = processor->Run(update_stmt);
  CHECK(s.ok());

  // Sleep for 1.1 seconds and verify ttl has expired.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  // c1 = 1 should still exist.
  auto row_block = ExecSelect(processor);
  YQLRow& row = row_block->row(0);

  EXPECT_EQ(1, row.column(0).int32_value());
  EXPECT_TRUE(row.column(1).IsNull());
  EXPECT_TRUE(row.column(2).IsNull());

  // Try an update by setting the primary key, which should fail since set clause can't have
  // primary keys.
  std::string invalid_update_stmt("UPDATE test_table USING TTL 1 SET c1 = 4 WHERE c1 = 1;");
  s = processor->Run(invalid_update_stmt);
  CHECK(!s.ok());
}

} // namespace sql
} // namespace yb
