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

#include "yb/util/decimal.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

namespace yb {
namespace ql {

class TestQLQuery : public QLTestBase {
 public:
  TestQLQuery() : QLTestBase() {
  }
};

TEST_F(TestQLQuery, TestQLQuerySimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  const char *create_stmt = "CREATE TABLE test_table"
    "(h0 tinyint, h1 smallint, h2 int, h3 bigint, h4 varchar, h5 varint,"
    " r0 tinyint, r1 smallint, r2 int, r3 bigint, r4 varchar, r5 varint,"
    " v0 tinyint, v1 smallint, v2 int, v3 bigint, v4 varchar,"
    " v5 float, v6 double precision, v7 boolean, v8 varint,"
    " primary key((h0, h1, h2, h3, h4, h5), r0, r1, r2, r3, r4, r5));";
  CHECK_VALID_STMT(create_stmt);

  // Test NOTFOUND. Select from empty table for all types.
  CHECK_VALID_STMT("SELECT * FROM test_table");
  CHECK_VALID_STMT("SELECT * FROM test_table"
                   "  WHERE h0 = 0 AND h1 = 0 AND h2 = 0 AND h3 = 0 AND h4 = 'zero' AND h5 = 0;");
  auto empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);

  // Insert 10 rows into the table.
  string stmt;
  static const int kNumRows = 10;
  for (int idx = 0; idx < kNumRows; idx++) {
    const char *bool_value = idx % 2 == 0 ? "true" : "false";

    // INSERT: Valid statement with column list.
    stmt = Substitute(
      "INSERT INTO test_table"
      "(h0, h1, h2, h3, h4, h5,"
      " r0, r1, r2, r3, r4, r5,"
      " v0, v1, v2, v3, v4, v5, v6, v7, v8)"
      " VALUES($0, $0, $0, $0, 'h$0', $0,"
      "        $1, $1, $1, $1, 'r$1', $1,"
      "        $0, $2, $2, $2, 'v$2', $2, $2, $3, $2);",
      idx, idx+100, idx+200, bool_value);
    LOG(INFO) << "Executing " << stmt;
    CHECK_VALID_STMT(stmt);
  }
  LOG(INFO) << kNumRows << " rows inserted";

  // Test simple query and result.
  CHECK_VALID_STMT("SELECT * FROM test_table "
                   "  WHERE h0 = 7 AND h1 = 7 AND h2 = 7 AND h3 = 7 AND h4 = 'h7' AND h5 = 7;");

  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const auto& row = row_block->row(0);
  CHECK_EQ(row.column(0).int8_value(), 7);
  CHECK_EQ(row.column(1).int16_value(), 7);
  CHECK_EQ(row.column(2).int32_value(), 7);
  CHECK_EQ(row.column(3).int64_value(), 7);
  CHECK_EQ(row.column(4).string_value(), "h7");
  CHECK_EQ(row.column(5).varint_value(), VarInt(7));
  CHECK_EQ(row.column(6).int8_value(), 107);
  CHECK_EQ(row.column(7).int16_value(), 107);
  CHECK_EQ(row.column(8).int32_value(), 107);
  CHECK_EQ(row.column(9).int64_value(), 107);
  CHECK_EQ(row.column(10).string_value(), "r107");
  CHECK_EQ(row.column(11).varint_value(), VarInt(107));
  CHECK_EQ(row.column(12).int8_value(), 7);
  CHECK_EQ(row.column(13).int16_value(), 207);
  CHECK_EQ(row.column(14).int32_value(), 207);
  CHECK_EQ(row.column(15).int64_value(), 207);
  CHECK_EQ(row.column(16).string_value(), "v207");
  int ival = row.column(17).float_value();
  CHECK(ival >= 206 && ival <= 207);
  ival = row.column(18).double_value();;
  CHECK(ival >= 206 && ival <= 207);
  CHECK_EQ(row.column(19).bool_value(), false);
  CHECK_EQ(row.column(20).varint_value(), VarInt(207));
}

#define CHECK_EXPECTED_ROW_DECIMAL(processor, name, balance, rate)                               \
do {                                                                                             \
  auto row_block = processor->row_block();                                                       \
  CHECK_EQ(row_block->row_count(), 1);                                                           \
  CHECK_EQ(row_block->row(0).column(0).string_value(), name);                                    \
  util::Decimal expected_decimal(balance), ret_decimal;                                          \
  auto s = ret_decimal.DecodeFromComparable(row_block->row(0).column(1).decimal_value());        \
  CHECK(s.ok());                                                                                 \
  CHECK_EQ(ret_decimal, expected_decimal);                                                       \
  CHECK_EQ(row_block->row(0).column(2).double_value(), rate);                                    \
} while(0)

#define CHECK_EXPECTED_ROW_VARINT(processor, name, balance, rate)                                \
do {                                                                                             \
  auto row_block = processor->row_block();                                                       \
  CHECK_EQ(row_block->row_count(), 1);                                                           \
  CHECK_EQ(row_block->row(0).column(0).string_value(), name);                                    \
  VarInt expected_varint = CHECK_RESULT(VarInt::CreateFromString(balance));          \
  VarInt ret_varint;                                                                       \
  ret_varint = row_block->row(0).column(1).varint_value();                                       \
  CHECK_EQ(ret_varint, expected_varint);                                                         \
  CHECK_EQ(row_block->row(0).column(2).double_value(), rate);                                    \
} while(0)

TEST_F(TestQLQuery, TestQLDecimalType) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  const char *create_stmt =
      "CREATE TABLE accounts(name varchar, balance decimal, rate double, primary key(name))";
  CHECK_VALID_STMT(create_stmt);

  string balance = "123456789123456789123456789123456789123456789123456390482039482309482309481.99";
  double rate = .01;
  string stmt = Substitute("INSERT INTO accounts(name, balance, rate) VALUES('neil', $0, $1)",
                    balance, rate);
  LOG(INFO) << "Executing " << stmt;
  CHECK_VALID_STMT(stmt);
  CHECK_VALID_STMT("SELECT name, balance, rate FROM accounts WHERE name='neil'");
  CHECK_EXPECTED_ROW_DECIMAL(processor, "neil", balance, rate);
}

TEST_F(TestQLQuery, TestQLVarIntType) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  const char *create_stmt =
      "CREATE TABLE accounts(name varchar, balance varint, rate double, primary key(name))";
  CHECK_VALID_STMT(create_stmt);

  string balance = "123456789123456789123456789123456789123456789123456390482039482309482309481";
  double rate = .01;
  string stmt = Substitute("INSERT INTO accounts(name, balance, rate) VALUES('sagnik', $0, $1)",
                    balance, rate);
  LOG(INFO) << "Executing " << stmt;
  CHECK_VALID_STMT(stmt);
  CHECK_VALID_STMT("SELECT name, balance, rate FROM accounts WHERE name='sagnik'");
  CHECK_EXPECTED_ROW_VARINT(processor, "sagnik", balance, rate);
}

TEST_F(TestQLQuery, TestQLDecimalTypeInKey) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  auto create_stmt = "CREATE TABLE accounts(name varchar, balance decimal, rate double, "
                     "primary key(name, balance))";
  CHECK_VALID_STMT(create_stmt);

  vector<string> names = { "hector", "kannan", "karthik", "neil" };
  vector<string> balances = {
      "1.0",
      "100.01",
      "100.02",
      "123456789123456789123456789123456789123456789123456390482039482309482309481.99"
  };
  vector<double> rates = { .0001, .022, 0001, .0001 };

  for (size_t i = 0; i < names.size(); i++) {
    auto insert_stmt = Substitute("INSERT INTO accounts(name, balance, rate) VALUES('$0', $1, $2)",
                                  names[i], balances[i], rates[i]);
    LOG(INFO) << "Executing: " << insert_stmt;
    CHECK_VALID_STMT(insert_stmt);
    auto select_stmt1 =
        Substitute("SELECT name, balance, rate FROM accounts WHERE name = '$0' AND balance = $1",
                   names[i], balances[i]);
    LOG(INFO) << "Executing: " << select_stmt1;
    CHECK_VALID_STMT(select_stmt1);
    CHECK_EXPECTED_ROW_DECIMAL(processor, names[i], balances[i], rates[i]);

    auto select_stmt2 =
        Substitute("SELECT name, balance, rate FROM accounts WHERE name = '$0' AND balance > 0.001",
                   names[i]);
    LOG(INFO) << "Executing: " << select_stmt2;
    CHECK_VALID_STMT(select_stmt2);
    CHECK_EXPECTED_ROW_DECIMAL(processor, names[i], balances[i], rates[i]);

    auto select_stmt3 =
        Substitute("SELECT name, balance, rate FROM accounts WHERE name = '$0' AND balance < $1",
            names[i],
            "123456789123456789123456789123456789123456789123456390482039482309482309482.99");
    LOG(INFO) << "Executing : " << select_stmt3;
    CHECK_VALID_STMT(select_stmt3);
    CHECK_EXPECTED_ROW_DECIMAL(processor, names[i], balances[i], rates[i]);
  }
}

TEST_F(TestQLQuery, TestQLVarIntTypeInKey) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  auto create_stmt = "CREATE TABLE accounts(name varchar, balance varint, rate double, "
      "primary key(name, balance))";
  CHECK_VALID_STMT(create_stmt);

  vector<string> names = { "hector", "kannan", "karthik", "sagnik" };
  vector<string> balances = {
      "-6555627",
      "0",
      "234",
      "123456789123456789123456789123456789123456789123456390482039482309482309481"
  };
  vector<double> rates = { .0001, .022, 0001, .0001 };

  for (size_t i = 0; i < names.size(); i++) {
    auto insert_stmt = Substitute("INSERT INTO accounts(name, balance, rate) VALUES('$0', $1, $2)",
                                  names[i], balances[i], rates[i]);
    LOG(INFO) << "Executing: " << insert_stmt;
    CHECK_VALID_STMT(insert_stmt);
    auto select_stmt1 =
        Substitute("SELECT name, balance, rate FROM accounts WHERE name = '$0' AND balance = $1",
                   names[i], balances[i]);
    LOG(INFO) << "Executing: " << select_stmt1;
    CHECK_VALID_STMT(select_stmt1);
    CHECK_EXPECTED_ROW_VARINT(processor, names[i], balances[i], rates[i]);

    auto select_stmt2 =
        Substitute("SELECT name, balance, rate FROM accounts WHERE name = '$0' AND balance > $1",
                   names[i],
                   "-99988989898988989898989876454542344567754322121356576877980000765313347");
    LOG(INFO) << "Executing: " << select_stmt2;
    CHECK_VALID_STMT(select_stmt2);
    CHECK_EXPECTED_ROW_VARINT(processor, names[i], balances[i], rates[i]);

    auto select_stmt3 =
        Substitute("SELECT name, balance, rate FROM accounts WHERE name = '$0' AND balance < $1",
                   names[i],
                   "123456789123456789123456789123456789123456789123456390482039482309482309482");
    LOG(INFO) << "Executing : " << select_stmt3;
    CHECK_VALID_STMT(select_stmt3);
    CHECK_EXPECTED_ROW_VARINT(processor, names[i], balances[i], rates[i]);
  }
}

} // namespace ql
} // namespace yb
