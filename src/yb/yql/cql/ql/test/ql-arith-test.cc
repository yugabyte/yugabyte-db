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

#include "yb/common/ql_value.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;
using std::shared_ptr;

namespace yb {
namespace ql {

class TestQLArith : public QLTestBase {
 public:
  TestQLArith() : QLTestBase() {
  }
};

TEST_F(TestQLArith, TestQLArithVarint) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char *create_stmt =
      "CREATE TABLE test_varint(h1 int primary key, v1 varint, v2 varint, v3 varint);";
  CHECK_VALID_STMT(create_stmt);

  // Simple varint update
  CHECK_VALID_STMT("UPDATE test_varint SET v1 = 70000000000007, v2 = v2 + 77 WHERE h1 = 1;");

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_varint WHERE h1 = 1");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).varint_value(), VarInt(70000000000007));
  CHECK(row.column(2).IsNull());
  CHECK(row.column(3).IsNull());

  // Simple varint update
  CHECK_VALID_STMT("UPDATE test_varint SET v1 = v1 + 20, v2 = v1 + v1, v3 = v1 + v2 WHERE h1 = 1;");

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_varint WHERE h1 = 1");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& new_row = row_block->row(0);
  CHECK_EQ(new_row.column(0).int32_value(), 1);
  CHECK_EQ(new_row.column(1).varint_value(), VarInt(70000000000027));
  CHECK_EQ(new_row.column(2).varint_value(), VarInt(140000000000014));
  CHECK(new_row.column(3).IsNull());
}

TEST_F(TestQLArith, TestQLArithBigInt) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char *create_stmt =
      "CREATE TABLE test_bigint(h1 int primary key, c1 bigint, c2 bigint, c3 bigint);";
  CHECK_VALID_STMT(create_stmt);

  // Simple counter update
  CHECK_VALID_STMT("UPDATE test_bigint SET c1 = 77, c2 = c2 + 77 WHERE h1 = 1;");

  // Select counter.
  CHECK_VALID_STMT("SELECT * FROM test_bigint WHERE h1 = 1");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).int64_value(), 77);
  CHECK(row.column(2).IsNull());
  CHECK(row.column(3).IsNull());

  // Simple counter update
  CHECK_VALID_STMT("UPDATE test_bigint SET c1 = c1 + 20, c2 = c1 + c1, c3 = c1 + c2 WHERE h1 = 1;");

  // Select counter.
  CHECK_VALID_STMT("SELECT * FROM test_bigint WHERE h1 = 1");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& new_row = row_block->row(0);
  CHECK_EQ(new_row.column(0).int32_value(), 1);
  CHECK_EQ(new_row.column(1).int64_value(), 97);
  CHECK_EQ(new_row.column(2).int64_value(), 154);
  CHECK(new_row.column(3).IsNull());
}

TEST_F(TestQLArith, TestQLArithInt) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE test_int(h1 int primary key, c1 int, c2 int, c3 smallint, c4 tinyint);";
  CHECK_VALID_STMT(create_stmt);

  // Simple counter update
  CHECK_VALID_STMT("UPDATE test_int SET c1 = 77, c2 = c2 + 77 WHERE h1 = 1;");

  // Select counter.
  CHECK_VALID_STMT("SELECT * FROM test_int WHERE h1 = 1");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).int32_value(), 77);
  CHECK(row.column(2).IsNull());
  CHECK(row.column(3).IsNull());
  CHECK(row.column(4).IsNull());

  // Simple counter update
  CHECK_VALID_STMT("UPDATE test_int SET c1 = c1 + 20, c2 = c1 + c1, c3 = c1 + 3, c4 = c1 + 4"
                   "  WHERE h1 = 1;");

  // Select counter.
  CHECK_VALID_STMT("SELECT * FROM test_int WHERE h1 = 1");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& new_row = row_block->row(0);
  CHECK_EQ(new_row.column(0).int32_value(), 1);
  CHECK_EQ(new_row.column(1).int32_value(), 97);
  CHECK_EQ(new_row.column(2).int32_value(), 154);
  CHECK_EQ(new_row.column(3).int16_value(), 80);
  CHECK_EQ(new_row.column(4).int8_value(), 81);
}

TEST_F(TestQLArith, TestQLArithCounter) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE test_counter(h1 int primary key, c1 counter, c2 counter, c3 counter);";
  CHECK_VALID_STMT(create_stmt);

  // Simple counter update
  CHECK_VALID_STMT("UPDATE test_counter SET c1 = c1 + 77 WHERE h1 = 1;");

  // Select counter.
  CHECK_VALID_STMT("SELECT * FROM test_counter WHERE h1 = 1");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).int64_value(), 77);

  // Simple counter update
  CHECK_VALID_STMT("UPDATE test_counter SET c1 = c1 + 10 WHERE h1 = 1;");

  // Select counter.
  CHECK_VALID_STMT("SELECT * FROM test_counter WHERE h1 = 1");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& new_row = row_block->row(0);
  CHECK_EQ(new_row.column(0).int32_value(), 1);
  CHECK_EQ(new_row.column(1).int64_value(), 87);
}

TEST_F(TestQLArith, TestQLErrorArithCounter) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE test_counter(h1 int primary key, c1 counter, c2 counter, c3 counter);";
  CHECK_VALID_STMT(create_stmt);

  // Insert is not allowed.
  CHECK_INVALID_STMT("INSERT INTO test_counter(h1, c1) VALUES(1, 2);");

  // Update with constant.
  CHECK_INVALID_STMT("UPDATE test_counter SET c1 = 2 WHERE h1 = 1;");

  // Update with wrong column.
  CHECK_INVALID_STMT("UPDATE test_counter SET c1 = c2 + 3 WHERE h1 = 2;");
}

TEST_F(TestQLArith, TestSimpleArithExpr) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  const char *create_stmt = "CREATE TABLE test_expr(h int, r int, v int, primary key(h, r));";
  CHECK_VALID_STMT(create_stmt);

  //------------------------------------------------------------------------------------------------
  // Test Insert: VALUES clause.

  // Hash columns.
  EXEC_VALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(1 + 1, 1, 1);");

  // Range columns.
  EXEC_VALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(1, 1 + 1, 1);");

  // Regular columns.
  EXEC_VALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(1, 1, 1 + 1);");

  // Expressions with wrong type are not allowed.
  EXEC_INVALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(3, 1, 'a' + 'b');");
  EXEC_INVALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(2 + 3.0, 1, 1);");

  // Expressions with column references not allowed as values.
  CHECK_INVALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(1 + v, 1, 1);");
  CHECK_INVALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(1, h + 1, 1);");
  CHECK_INVALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(1, 1, r + 1);");
  CHECK_INVALID_STMT("INSERT INTO test_expr(h, r, v) VALUES(2, 1, h + r);");

  //------------------------------------------------------------------------------------------------
  // Test Select: SELECT and WHERE clauses.

  // Test various selects.
  std::shared_ptr<qlexpr::QLRowBlock> row_block;

  // Selecting expressions.
  CHECK_VALID_STMT("SELECT 1 + 2, h, 2 + 3 FROM test_expr WHERE h = 2");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_EQ(processor->row_block()->row(0).column(0).int64_value(), 3);
  CHECK_EQ(processor->row_block()->row(0).column(1).int32_value(), 2);
  CHECK_EQ(processor->row_block()->row(0).column(2).int64_value(), 5);

  // Expressions for hash key columns.
  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 + 1");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_EQ(processor->row_block()->row(0).column(0).int32_value(), 2);

  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 + 1 AND r = 1");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_EQ(processor->row_block()->row(0).column(0).int32_value(), 2);

  // Expressions for range key columns.
  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE r = 1 + 1");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_EQ(processor->row_block()->row(0).column(1).int32_value(), 2);

  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 AND r = 1 + 1");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_EQ(processor->row_block()->row(0).column(1).int32_value(), 2);

  // Expressions for regular columns.
  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE v = 1 + 1;");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 AND v = 1 + 1;");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE r = 1 AND v = 1 + 1;");
  CHECK_EQ(processor->row_block()->row_count(), 1);
  CHECK_VALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 AND r = 1 AND v = 1 + 1;");
  CHECK_EQ(processor->row_block()->row_count(), 1);

  // Expressions with column references are not allowed in RHS of where clause.
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE h = r + 1");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE h = r + 1 AND r = 1");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE r = h + 1");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 AND r = h + 1");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE v = r + 1;");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 AND v = h + 1;");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE r = 1 AND v = r + 1;");
  CHECK_INVALID_STMT("SELECT h, r, v FROM test_expr WHERE h = 1 AND r = 1 AND v = r + 1;");

  //------------------------------------------------------------------------------------------------
  // Test Update: WHERE, IF and SET clauses.

  CHECK_VALID_STMT("UPDATE test_expr SET v = 1 + 2 WHERE h = 1 + 1 AND r = 1;");
  CHECK_VALID_STMT("UPDATE test_expr SET v = 2 + 1 WHERE h = 1 AND r = 1 + 1;");
  CHECK_VALID_STMT("UPDATE test_expr SET v = 1 + 2 WHERE h = 1 AND r = 1 IF v = 1 + 1;");

  // Check rows got updated, all three rows should now have value 3.
  CHECK_VALID_STMT("SELECT v FROM test_expr;");
  CHECK_EQ(processor->row_block()->row_count(), 3);
  CHECK_EQ(processor->row_block()->row(0).column(0).int32_value(), 3);
  CHECK_EQ(processor->row_block()->row(1).column(0).int32_value(), 3);
  CHECK_EQ(processor->row_block()->row(2).column(0).int32_value(), 3);

  // Expressions with column references are not allowed in RHS of if/where clauses.
  CHECK_INVALID_STMT("UPDATE test_expr SET v = 1 WHERE h = r + 1 AND r = 1;");
  CHECK_INVALID_STMT("UPDATE test_expr SET v = 1 WHERE h = 1 AND r = h + 1;");
  CHECK_INVALID_STMT("UPDATE test_expr SET v = 1 WHERE h = 1 AND r = 1 IF v = h + 1;");

  //------------------------------------------------------------------------------------------------
  // Test Delete: WHERE clause.

  CHECK_VALID_STMT("DELETE FROM test_expr where h = 1 + 1 AND r = 1;");
  CHECK_VALID_STMT("DELETE FROM test_expr where h = 1 AND r = 1 + 1;");
  CHECK_VALID_STMT("DELETE FROM test_expr where h = 1 AND r = 1 IF v = 1 + 2;");

  // Check rows got deleted.
  CHECK_VALID_STMT("SELECT * FROM test_expr;");
  CHECK_EQ(processor->row_block()->row_count(), 0);

  // Expressions with column references are not allowed in RHS of if/where clauses.
  CHECK_INVALID_STMT("DELETE FROM test_expr WHERE h = r + 1 AND r = 1;");
  CHECK_INVALID_STMT("UPDATE FROM test_expr WHERE h = 1 AND r = h + 1;");
  CHECK_INVALID_STMT("UPDATE FROM test_expr WHERE h = 1 AND r = 1 IF v = h + 1;");

}

} // namespace ql
} // namespace yb
