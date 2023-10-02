//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <cmath>
#include <chrono>
#include <limits>
#include <thread>

#include "yb/common/jsonb.h"
#include "yb/qlexpr/ql_serialization.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/util/decimal.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/statement.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/yql/cql/ql/util/cql_message.h"
#include "yb/yql/cql/ql/util/errcodes.h"

DECLARE_bool(TEST_tserver_timeout);

using std::string;
using std::shared_ptr;
using std::numeric_limits;
using std::vector;

using strings::Substitute;
using yb::util::Decimal;
using yb::util::DecimalFromComparable;
using yb::VarInt;
using namespace std::chrono_literals;

namespace yb {
namespace ql {

using qlexpr::QLRowBlock;

struct CQLQueryParameters : public CQLMessage::QueryParameters {
  typedef CQLMessage::QueryParameters::NameToIndexMap NameToIndexMap;

  CQLQueryParameters() {
    flags = CQLMessage::QueryParameters::kWithValuesFlag;
  }

  void Reset() {
    values.clear();
    value_map.clear();
  }

  void PushBackInt32(const string& name, int32_t val) {
    QLValue qv;
    qv.set_int32_value(val);
    PushBack(name, qv, DataType::INT32);
  }

  void PushBackString(const string& name, const string& val) {
    QLValue qv;
    qv.set_string_value(val);
    PushBack(name, qv, DataType::STRING);
  }

  void PushBack(const string& name, const QLValue& qv, DataType data_type) {
    PushBack(name, qv, QLType::Create(data_type));
  }

  void PushBack(const string& name, const QLValue& qv, const shared_ptr<QLType>& type) {
    WriteBuffer buffer(1024);
    qlexpr::SerializeValue(type, YQL_CLIENT_CQL, qv.value(), &buffer);

    CQLMessage::Value msg_value;
    msg_value.name = name;
    msg_value.value = buffer.ToBuffer();
    value_map.insert(NameToIndexMap::value_type(name, values.size()));
    values.push_back(msg_value);
  }
};

class QLTestSelectedExpr : public QLTestBase {
 public:
  QLTestSelectedExpr() : QLTestBase() {
  }

  void CheckSelectedRow(TestQLProcessor *processor,
                        const string& query,
                        const string& expected_row) const {
    CHECK_VALID_STMT(query);
    auto row_block = processor->row_block();
    EXPECT_EQ(row_block->row_count(), 1);
    if (row_block->row_count() > 0) {
      std::string row_str = row_block->row(0).ToString();
      LOG(INFO) << "Got row: " << row_str;
      if(row_str != expected_row) {
        DumpDocDB(cluster_.get());
        EXPECT_EQ(row_str, expected_row);
      }
    }
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

  // Insert rows whose hash value is '1'.
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

  std::shared_ptr<qlexpr::QLRowBlock> row_block;

  //------------------------------------------------------------------------------------------------
  // Test COUNT() aggregate function.
  {
    // Test COUNT() - Not existing data.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int64_value(), 0);

    // Test COUNT() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(1).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(2).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(3).int64_value(), 1);

    // Test COUNT() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(1).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(2).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(3).int64_value(), 2);

    // Test COUNT() - All rows.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_all_row = row_block->row(0);
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
    const auto& sum_0_row = row_block->row(0);
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
    const auto& sum_1_row = row_block->row(0);
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
    const auto& sum_2_row = row_block->row(0);
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
    const auto& sum_all_row = row_block->row(0);
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
    const auto& sum_0_row = row_block->row(0);
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
    const auto& sum_1_row = row_block->row(0);
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
    const auto& sum_2_row = row_block->row(0);
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
    const auto& sum_all_row = row_block->row(0);
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
    const auto& sum_0_row = row_block->row(0);
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
    const auto& sum_1_row = row_block->row(0);
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
    const auto& sum_2_row = row_block->row(0);
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
    const auto& sum_all_row = row_block->row(0);
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

TEST_F(QLTestSelectedExpr, TestAggregateExprWithNull) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions with NULL column 'v2'.";

  // Create the table and insert some value.
  const char *create_stmt =
    "CREATE TABLE test_aggr_expr(h int, r int,"
    "                            v1 bigint, v2 int, v3 smallint, v4 tinyint,"
    "                            v5 float, v6 double, v7 text, primary key(h, r));";
  CHECK_VALID_STMT(create_stmt);

  // Insert rows whose hash value is '1'.
  // v2 = NULL - for all, v1 = NULL - first only, v7 = NULL - except first & second,
  // v3,v4,v5,v6 = NULL second only.
  CHECK_VALID_STMT("INSERT INTO test_aggr_expr(h, r, v3, v4, v5, v6, v7)" // v1, v2 = NULL
                   " VALUES(1, 777, 13, 14, 15, 16, 'aaa');");
  CHECK_VALID_STMT("INSERT INTO test_aggr_expr(h, r, v1, v7)" // v2, v3, v4, v5, v6 = NULL
                   " VALUES(1, 888, 11, 'bbb');");

  // Insert the rest of the rows, one of which has hash value of '1'.
  int64_t v1_total = 11;
  int16_t v3_total = 13;
  int8_t v4_total = 14;
  float v5_total = 15;
  double v6_total = 16;
  for (int i = 1; i < 20; i++) {
    string stmt = strings::Substitute(
        "INSERT INTO test_aggr_expr(h, r, v1, v3, v4, v5, v6)" // v2, v7 = NULL
        " VALUES($0, $1, $2, $3, $4, $5, $6);",
        i, i + 1, i + 1000, i + 10, i, i + 77.77, i + 999.99);
    CHECK_VALID_STMT(stmt);

    v1_total += (i + 1000);
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
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(4).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(5).int64_value(), 0);

    // Test COUNT() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(1).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(2).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(3).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_1_row.column(4).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_1_row.column(5).int64_value(), 1);

    // Test COUNT() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 3);
    CHECK_EQ(sum_2_row.column(1).int64_value(), 3);
    CHECK_EQ(sum_2_row.column(2).int64_value(), 3);
    CHECK_EQ(sum_2_row.column(3).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(4).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_2_row.column(5).int64_value(), 2);

    // Test COUNT() - All rows.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 21);
    CHECK_EQ(sum_all_row.column(1).int64_value(), 21);
    CHECK_EQ(sum_all_row.column(2).int64_value(), 21);
    CHECK_EQ(sum_all_row.column(3).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(4).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_all_row.column(5).int64_value(), 2);
  }

  //------------------------------------------------------------------------------------------------
  // Test SUM() aggregate function. NOTE: SUM(v7) - is not applicable for TEXT type.
  {
    // Test SUM() - Not existing data.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_0_row = row_block->row(0);
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
    const auto& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 0); // Only one NULL value.
    CHECK_EQ(sum_1_row.column(1).int32_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test SUM() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1012);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 0); // NULL values are not counted.
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
    const auto& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), v1_total);
    CHECK_EQ(sum_all_row.column(1).int32_value(), 0); // NULL values are not counted.
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
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());
    CHECK(sum_0_row.column(6).IsNull());

    // Test MAX() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_1_row = row_block->row(0);
    CHECK(sum_1_row.column(0).IsNull()); // NULL value.
    CHECK(sum_1_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);
    CHECK_EQ(sum_1_row.column(6).string_value(), "aaa");

    // Test MAX() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1001);
    CHECK(sum_2_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_2_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 14);
    // Comparing floating point for 78.77
    CHECK_GT(sum_2_row.column(4).float_value(), 78.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 78.775);
    // Comparing floating point for 1000.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1000.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1000.995);
    CHECK_EQ(sum_2_row.column(6).string_value(), "bbb");

    // Test MAX() - All rows.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 1019);
    CHECK(sum_all_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_all_row.column(2).int16_value(), 29);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 19);
    float v5_max = 96.77;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_max - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_max + 0.1);
    double v6_max = 1018.99;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_max - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_max + 0.1);
    CHECK_EQ(sum_all_row.column(6).string_value(), "bbb");
  }

  //------------------------------------------------------------------------------------------------
  // Test MIN() aggregate functions.
  {
    // Test MIN() - Not exist.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());
    CHECK(sum_0_row.column(6).IsNull());

    // Test MIN() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_1_row = row_block->row(0);
    CHECK(sum_1_row.column(0).IsNull()); // NULL value.
    CHECK(sum_1_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);
    CHECK_EQ(sum_1_row.column(6).string_value(), "aaa");

    // Test MIN() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 11);
    CHECK(sum_2_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_2_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 1);
    // Comparing floating point for 15
    CHECK_GT(sum_2_row.column(4).float_value(), 14.9);
    CHECK_LT(sum_2_row.column(4).float_value(), 15.1);
    // Comparing floating point for 16
    CHECK_GT(sum_2_row.column(5).double_value(), 15.9);
    CHECK_LT(sum_2_row.column(5).double_value(), 16.1);
    CHECK_EQ(sum_2_row.column(6).string_value(), "aaa");

    // Test MIN() - All rows.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 11);
    CHECK(sum_all_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_all_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 1);
    float v5_min = 15;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_min - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_min + 0.1);
    double v6_min = 16;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_min - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_min + 0.1);
    CHECK_EQ(sum_all_row.column(6).string_value(), "aaa");
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
  const auto& star_row = row_block->row(0);
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
  const auto& expr_row = row_block->row(0);
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
  const auto& expr_alias_row = row_block->row(0);
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
  const auto& row1 = row_block->row(0);
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
  const auto& row2 = row_block->row(0);
  CHECK_EQ(row2.column(0).int32_value(), 11);
  CHECK_EQ(row2.column(1).double_value(), 22.5);
  CHECK_EQ(row2.column(2).string_value(), "bc");
}

TEST_F(QLTestSelectedExpr, TestQLSelectToJson) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  std::shared_ptr<QLRowBlock> row_block;
  LOG(INFO) << "Test selecting with ToJson() built-in.";

  auto to_json_str = [](const QLValue& value) -> string {
    common::Jsonb jsonb(value.jsonb_value());
    string str;
    CHECK_OK(jsonb.ToJsonString(&str));
    return str;
  };

  // Test various selects.

  // Create the user-defined-type, table with UDT & FROZEN and insert some value.
  CHECK_VALID_STMT("CREATE TYPE udt(v1 int, v2 int)");
  CHECK_VALID_STMT("CREATE TABLE test_udt (h int PRIMARY KEY, s SET<int>, u udt, "
                   "f FROZEN<set<int>>, sf SET<FROZEN<set<int>>>, su SET<FROZEN<udt>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt (h, s, u, f, sf, su) values (1, "
                   "{1,2}, {v1:3,v2:4}, {5,6}, {{7,8}}, {{v1:9,v2:0}})");

  // Apply ToJson() to the key column.
  CHECK_VALID_STMT("SELECT tojson(h) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("1", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the SET.
  CHECK_VALID_STMT("SELECT tojson(s) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[1,2]", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the UDT column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"v1\":3,\"v2\":4}", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the FROZEN<SET> column.
  CHECK_VALID_STMT("SELECT tojson(f) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[5,6]", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the SET<FROZEN<SET>> column.
  CHECK_VALID_STMT("SELECT tojson(sf) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[[7,8]]", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the SET<FROZEN<UDT>> column.
  CHECK_VALID_STMT("SELECT tojson(su) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[{\"v1\":9,\"v2\":0}]", to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt2 (h int PRIMARY KEY, u frozen<udt>)");
  CHECK_VALID_STMT("INSERT INTO test_udt2 (h, u) values (1, {v1:33,v2:44})");
  // Apply ToJson() to the FROZEN<UDT> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt2");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"v1\":33,\"v2\":44}", to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt3 (h int PRIMARY KEY, u list<frozen<udt>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt3 (h, u) values (1, [{v1:44,v2:55}, {v1:66,v2:77}])");
  // Apply ToJson() to the LIST<UDT> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt3");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[{\"v1\":44,\"v2\":55},{\"v1\":66,\"v2\":77}]",
            to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt4 (h int PRIMARY KEY, "
                   "u map<frozen<udt>, frozen<udt>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt4 (h, u) values "
                   "(1, {{v1:44,v2:55}:{v1:66,v2:77}, {v1:88,v2:99}:{v1:11,v2:22}})");
  // Apply ToJson() to the MAP<FROZEN<UDT>:FROZEN<UDT>> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt4");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(("{\"{\\\"v1\\\":44,\\\"v2\\\":55}\":{\"v1\":66,\"v2\":77},"
             "\"{\\\"v1\\\":88,\\\"v2\\\":99}\":{\"v1\":11,\"v2\":22}}"),
            to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt5 (h int PRIMARY KEY, "
                   "u map<frozen<list<frozen<udt>>>, frozen<set<frozen<udt>>>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt5 (h, u) values "
                   "(1, {[{v1:44,v2:55}, {v1:66,v2:77}]:{{v1:88,v2:99},{v1:11,v2:22}}})");
  // Apply ToJson() to the MAP<FROZEN<LIST<FROZEN<UDT>>>:FROZEN<SET<FROZEN<UDT>>>> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt5");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(("{\"[{\\\"v1\\\":44,\\\"v2\\\":55},{\\\"v1\\\":66,\\\"v2\\\":77}]\":"
             "[{\"v1\":11,\"v2\":22},{\"v1\":88,\"v2\":99}]}"),
            to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt6 (h int PRIMARY KEY, "
                   "u map<frozen<map<frozen<udt>, text>>, frozen<set<frozen<udt>>>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt6 (h, u) values "
                   "(1, {{{v1:11,v2:22}:'text'}:{{v1:55,v2:66},{v1:77,v2:88}}})");
  // Apply ToJson() to the MAP<FROZEN<MAP<FROZEN<UDT>:TEXT>>:FROZEN<SET<FROZEN<UDT>>>>
  // column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt6");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(("{\"{\\\"{\\\\\\\"v1\\\\\\\":11,\\\\\\\"v2\\\\\\\":22}\\\":\\\"text\\\"}\":"
             "[{\"v1\":55,\"v2\":66},{\"v1\":77,\"v2\":88}]}"),
            to_json_str(row_block->row(0).column(0)));

  // Test UDT with case-sensitive field names and names with spaces.
  CHECK_VALID_STMT("CREATE TYPE udt7(v1 int, \"V2\" int, \"v  3\" int, \"V  4\" int)");
  CHECK_VALID_STMT("CREATE TABLE test_udt7 (h int PRIMARY KEY, u udt7)");
  CHECK_VALID_STMT("INSERT INTO test_udt7 (h, u) values "
                   "(1, {v1:11,\"V2\":22,\"v  3\":33,\"V  4\":44})");
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt7");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  // Verify that the column names in upper case are double quoted (see the case in Cassandra).
  EXPECT_EQ("{\"\\\"V  4\\\"\":44,\"\\\"V2\\\"\":22,\"v  3\":33,\"v1\":11}",
            to_json_str(row_block->row(0).column(0)));

  // Test UDT field which refers to another user-defined type.
  CHECK_VALID_STMT("CREATE TYPE udt8(i1 int, u1 frozen<udt>)");
  CHECK_VALID_STMT("CREATE TABLE test_udt_in_udt (h int PRIMARY KEY, u udt8)");
  CHECK_VALID_STMT("INSERT INTO test_udt_in_udt (h, u) values (1, {i1:33,u1:{v1:44,v2:55}})");
  // Apply ToJson() to the UDT< FROZEN<UDT> > column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt_in_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"i1\":33,\"u1\":{\"v1\":44,\"v2\":55}}",
            to_json_str(row_block->row(0).column(0)));

  // Test UDT field which refers to a FROZEN<LIST>.
  CHECK_VALID_STMT("CREATE TYPE udt9(i1 int, l1 frozen<list<int>>)");
  CHECK_VALID_STMT("CREATE TABLE test_list_in_udt (h int PRIMARY KEY, u udt9)");
  CHECK_VALID_STMT("INSERT INTO test_list_in_udt (h, u) values (1, {i1:77,l1:[4,5,6]})");
  // Apply ToJson() to the UDT< FROZEN<LIST> > column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_list_in_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"i1\":77,\"l1\":[4,5,6]}",
            to_json_str(row_block->row(0).column(0)));

  // Test UDT field which refers to a FROZEN<SET>.
  CHECK_VALID_STMT("CREATE TYPE udt10(i1 int, s1 frozen<set<int>>)");
  CHECK_VALID_STMT("CREATE TABLE test_set_in_udt (h int PRIMARY KEY, u udt10)");
  CHECK_VALID_STMT("INSERT INTO test_set_in_udt (h, u) values (1, {i1:66,s1:{3,2,1}})");
  // Apply ToJson() to the UDT< FROZEN<SET> > column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_set_in_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"i1\":66,\"s1\":[1,2,3]}",
            to_json_str(row_block->row(0).column(0)));

  // Test UDT field which refers to a FROZEN<MAP>.
  CHECK_VALID_STMT("CREATE TYPE udt11(i1 int, m1 frozen<map<int, text>>)");
  CHECK_VALID_STMT("CREATE TABLE test_map_in_udt (h int PRIMARY KEY, u udt11)");
  CHECK_VALID_STMT("INSERT INTO test_map_in_udt (h, u) values (1, {i1:88,m1:{99:'t1',11:'t2'}})");
  // Apply ToJson() to the UDT< FROZEN<MAP> > column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_map_in_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"i1\":88,\"m1\":{\"11\":\"t2\",\"99\":\"t1\"}}",
            to_json_str(row_block->row(0).column(0)));
}

TEST_F(QLTestSelectedExpr, TestCastDecimal) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting with CAST.";

  // Test conversions FROM DECIMAL TO numeric types.

  // Create the table and insert some decimal value.
  CHECK_VALID_STMT("CREATE TABLE num_decimal (pk int PRIMARY KEY, dc decimal)");

  // Invalid values.
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, NaN)");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, 'NaN')");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, Infinity)");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, 'Infinity')");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, 'a string')");

  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (123, 456)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT * FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 2);
    ASSERT_EQ(row.column(0).int32_value(), 123);
    EXPECT_EQ(DecimalFromComparable(row.column(1).decimal_value()), Decimal("456"));
  }
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("456"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as int) FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int32_value(), 456);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as double) FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 456.);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as float) FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 456.f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as text) FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).string_value(), "456");
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as decimal) FROM num_decimal");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("456"));
  }

  // Test value = MIN_BIGINT = -9,223,372,036,854,775,808 ~= -9.2E+18
  // (Using -9223372036854775807 instead of -9223372036854775808 due to a compiler
  // bug:  https://bugs.llvm.org/show_bug.cgi?id=21095)
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, -9223372036854775807)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=1");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-9223372036854775807"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS bigint) FROM num_decimal where pk=1");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int64_value(), -9223372036854775807LL);
  }
  {
    // INT32 overflow.
    CHECK_VALID_STMT("SELECT CAST(dc AS int) FROM num_decimal where pk=1");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int32_value(), static_cast<int32_t>(-9223372036854775807LL));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=1");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
              Decimal("-9223372036854775807"));
  }

  // Test value 123.4E+18 > MAX_BIGINT = 9,223,372,036,854,775,807 ~= 9.2E+18
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (2, 123456789012345678901)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=2");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("123456789012345678901"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=2");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("123456789012345678901"));
  }
  // INT64 overflow.
  CHECK_INVALID_STMT("SELECT CAST(dc AS bigint) FROM num_decimal where pk=2");
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(dc AS varint) FROM num_decimal where pk=2");

  // Test an extrim DECIMAL value.
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values "
                   "(3, -123123123123456456456456.789789789789123123123123)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=3");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-123123123123456456456456.789789789789123123123123"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=3");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-123123123123456456456456.789789789789123123123123"));
  }
  // INT64 overflow.
  CHECK_INVALID_STMT("SELECT CAST(dc AS bigint) FROM num_decimal where pk=3");
  CHECK_INVALID_STMT("SELECT CAST(dc AS int) FROM num_decimal where pk=3");

  // Test a value > MAX_DOUBLE=1.79769e+308.
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (4, 5e+308)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+308"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+308"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS float) FROM num_decimal where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // FLOAT overflow = Infinity.
    EXPECT_EQ(row.column(0).float_value(), numeric_limits<float>::infinity());
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS double) FROM num_decimal where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // DOUBLE overflow = Infinity.
    EXPECT_EQ(row.column(0).double_value(), numeric_limits<double>::infinity());
  }
  // Not supported.
  CHECK_INVALID_STMT("SELECT CAST(dc AS varint) FROM num_decimal where pk=4");

  // Test a value > MAX_FLOAT=3.40282e+38.
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (5, 5e+38)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+38"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+38"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS double) FROM num_decimal where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 5.e+38);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS float) FROM num_decimal where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // FLOAT overflow = Infinity.
    EXPECT_EQ(row.column(0).float_value(), numeric_limits<float>::infinity());
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(dc AS varint) FROM num_decimal where pk=5");

  // Test conversions FROM numeric types TO DECIMAL.

  // Create the table and insert some float value.
  CHECK_VALID_STMT("CREATE TABLE numbers (pk int PRIMARY KEY, flt float, dbl double, vari varint, "
                                         "i8 tinyint, i16 smallint, i32 int, i64 bigint)");
  CHECK_VALID_STMT("INSERT INTO numbers (pk, flt) values (1, 456.7)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT flt FROM numbers");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 456.7f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as float) FROM numbers");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 456.7f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as decimal) FROM numbers");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    double num = EXPECT_RESULT(DecimalFromComparable(row.column(0).decimal_value()).ToDouble());
    EXPECT_LT(fabs(num - 456.7), 0.001);
  }
  // Test -MAX_BIGINT=-9223372036854775807
  CHECK_VALID_STMT("INSERT INTO numbers (pk, i64) values (2, -9223372036854775807)");
  {
    CHECK_VALID_STMT("SELECT i64 FROM numbers where pk=2");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int64_value(), -9223372036854775807LL);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(i64 as bigint) FROM numbers where pk=2");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int64_value(), -9223372036854775807LL);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(i64 as decimal) FROM numbers where pk=2");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-9223372036854775807"));
  }
  // Test VARINT:
  CHECK_VALID_STMT("INSERT INTO numbers (pk, vari) values (3, "
                   "-123456789012345678901234567890123456789012345678901234567890)");
  {
    CHECK_VALID_STMT("SELECT vari FROM numbers where pk=3");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).varint_value(), CHECK_RESULT(VarInt::CreateFromString(
        "-123456789012345678901234567890123456789012345678901234567890")));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(vari as decimal) FROM numbers where pk=3");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-123456789012345678901234567890123456789012345678901234567890"));
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(vari as varint) FROM numbers where pk=3");

  // Test MAX_FLOAT=3.40282e+38
  CHECK_VALID_STMT("INSERT INTO numbers (pk, flt) values (4, 3.40282e+38)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT flt FROM numbers where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 3.40282e+38f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as float) FROM numbers where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 3.40282e+38f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as decimal) FROM numbers where pk=4");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    double num = EXPECT_RESULT(DecimalFromComparable(row.column(0).decimal_value()).ToDouble());
    EXPECT_LT(fabs(num - 3.40282e+38), 1e+31);
  }

  // Test MAX_DOUBLE=1.79769e+308
  CHECK_VALID_STMT("INSERT INTO numbers (pk, dbl) values (5, 1.79769e+308)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT dbl FROM numbers where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 1.79769e+308);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dbl as double) FROM numbers where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 1.79769e+308);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dbl as decimal) FROM numbers where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    double num = EXPECT_RESULT(DecimalFromComparable(row.column(0).decimal_value()).ToDouble());
    EXPECT_EQ(num, 1.79769e+308);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dbl AS float) FROM numbers where pk=5");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // FLOAT overflow = Infinity.
    EXPECT_EQ(row.column(0).float_value(), numeric_limits<float>::infinity());
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(dbl as varint) FROM numbers where pk=5");
}

TEST_F(QLTestSelectedExpr, TestCastTinyInt) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting with CAST.";

  // Try to convert FROM TINYINT TO a numeric type.

  // Create the table and insert some decimal value.
  CHECK_VALID_STMT("CREATE TABLE num_tinyint (pk int PRIMARY KEY, ti tinyint)");
  CHECK_VALID_STMT("INSERT INTO num_tinyint (pk, ti) values (1, 123)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT * FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 2);
    ASSERT_EQ(row.column(0).int32_value(), 1);
    ASSERT_EQ(row.column(1).int8_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT ti FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int8_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as smallint) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int16_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as int) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int32_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as bigint) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int64_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as double) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).double_value(), 123.);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as float) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).float_value(), 123.f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as text) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).string_value(), "123");
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as decimal) FROM num_tinyint");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("123"));
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(ti AS varint) FROM num_tinyint");
  // TINYINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(ti as tinyint) FROM num_tinyint");

  // Try value > MAX_TINYINT = 127
  CHECK_INVALID_STMT("INSERT INTO num_tinyint (pk, ti) values (2, 256)");

  // Try to convert FROM a numeric type TO TINYINT.

  // Create the table and insert some float value.
  CHECK_VALID_STMT("CREATE TABLE numbers (pk int PRIMARY KEY, flt float, dbl double, vari varint, "
                                         "i8 tinyint, i16 smallint, i32 int, i64 bigint)");
  CHECK_VALID_STMT("INSERT INTO numbers (pk, flt, dbl, vari, i8, i16, i32, i64) values "
                                       "(1, 456.7, 123.456, 256, 123, 123, 123, 123)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT i8 FROM numbers");
    auto row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int8_value(), 123);
  }
  // TINYINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(i16 as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(i32 as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(i64 as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(flt as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(dbl as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(vari as tinyint) FROM numbers");
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_timeout) = true;
  CHECK_INVALID_STMT("SELECT count(*) FROM test_table WHERE h = 1;");
}

TEST_F(QLTestSelectedExpr, ScanRangeTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND r1 = 5 AND r2 >= 5 AND r2 <= 6;");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const auto& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestReverse) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT(
      "SELECT * FROM test_range WHERE h = 5 AND r1 >= 5 AND r1 <= 6 AND r2 >= 5 AND r2 <= 6 ORDER "
      "BY r1 DESC;");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 4);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const auto& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const auto& row = row_block->row(2);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const auto& row = row_block->row(3);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestIncDec) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND r1 = 5 AND r2 >= 5 AND r2 <= 6;");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const auto& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestIncDecReverse) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT(
      "SELECT * FROM test_range WHERE h = 5 AND r1 >= 5 AND r1 <= 6 AND r2 >= 5 AND r2 <= 6 ORDER "
      "BY r1 DESC;");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 4);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const auto& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const auto& row = row_block->row(2);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const auto& row = row_block->row(3);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, ScanChoicesTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND r1 in (5) and r2 in (5, 6)");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const auto& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestIncDecAcrossHashCols) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  const int max_h = 48;
  for (int h = 0; h < max_h; h++) {
    for (int r1 = 0; r1 < 10; r1++) {
      for (int r2 = 0; r2 < 10; r2++) {
        CHECK_VALID_STMT(strings::Substitute(
            "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
      }
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT h, r1, r2, payload FROM test_range WHERE r1 = 5 AND r2 > 4 AND r2 < 7;");
  auto row_block = processor->row_block();
  EXPECT_EQ(row_block->row_count(), 2 * max_h);
  vector<bool> seen(max_h, false);
  for (size_t h = 0; h < row_block->row_count(); h++) {
    const auto& row = row_block->row(h);
    LOG(INFO) << "got " << row.ToString();
    seen[row.column(0).int32_value()] = true;
    EXPECT_EQ(row.column(1).int32_value(), 5);
    if (h % 2 == 0) {
      EXPECT_EQ(row.column(2).int32_value(), 6);
      EXPECT_EQ(row.column(3).int32_value(), 6);
    } else {
      EXPECT_EQ(row.column(2).int32_value(), 5);
      EXPECT_EQ(row.column(3).int32_value(), 5);
    }
  }
  CHECK_EQ(seen.size(), max_h);
  for (int h = 0; h < max_h; h++) {
    CHECK_EQ(seen[h], true);
  }
}

TEST_F(QLTestSelectedExpr, ScanChoicesTestIncDecAcrossHashCols) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  const int max_h = 48;
  for (int h = 0; h < max_h; h++) {
    for (int r1 = 0; r1 < 10; r1++) {
      for (int r2 = 0; r2 < 10; r2++) {
        CHECK_VALID_STMT(strings::Substitute(
            "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
      }
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT h, r1, r2, payload FROM test_range WHERE r1 in (5) AND r2 in (5, 6);");
  auto row_block = processor->row_block();
  EXPECT_EQ(row_block->row_count(), 2 * max_h);
  vector<bool> seen(max_h, false);
  for (size_t h = 0; h < row_block->row_count(); h++) {
    const auto& row = row_block->row(h);
    LOG(INFO) << "got " << row.ToString();
    seen[row.column(0).int32_value()] = true;
    EXPECT_EQ(row.column(1).int32_value(), 5);
    if (h % 2 == 0) {
      EXPECT_EQ(row.column(2).int32_value(), 6);
      EXPECT_EQ(row.column(3).int32_value(), 6);
    } else {
      EXPECT_EQ(row.column(2).int32_value(), 5);
      EXPECT_EQ(row.column(3).int32_value(), 5);
    }
  }
  CHECK_EQ(seen.size(), max_h);
  for (int h = 0; h < max_h; h++) {
    CHECK_EQ(seen[h], true);
  }
}

TEST_F(QLTestSelectedExpr, TestPreparedStatementWithCollections) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  PreparedResult::UniPtr result;

  LOG(INFO) << "Create and setup test table.";
  CHECK_VALID_STMT("CREATE TABLE test_tbl (h INT, r INT, "
                   "vm MAP<INT, TEXT>, vs set<INT>, vl list<TEXT>, PRIMARY KEY((h), r))");

  //----------------------------------------------------------------------------------------------
  // Testing Map.
  //----------------------------------------------------------------------------------------------
  // Test INSERT into MAP prepared statement.
  LOG(INFO) << "Prepare insert into MAP statement.";
  Statement insert_vm(processor->CurrentKeyspace(),
                      "INSERT INTO test_tbl (h, r, vm) VALUES(?, ?, ?);");
  EXPECT_OK(insert_vm.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  auto vm_binds = result->bind_variable_schemas();
  EXPECT_EQ(vm_binds.size(), 3);
  EXPECT_EQ(vm_binds[0].ToString(), "h[int32 NOT NULL VALUE]");
  EXPECT_EQ(vm_binds[1].ToString(), "r[int32 NOT NULL VALUE]");
  EXPECT_EQ(vm_binds[2].ToString(), "vm[map NOT NULL VALUE]");
  EXPECT_EQ(vm_binds[2].type()->ToString(), "map<int, text>");

  // Bind and execute the prepared statement with correct values.
  QLValue qvm;
  qvm.add_map_key()->set_int32_value(2);
  qvm.add_map_value()->set_string_value("b");
  qvm.add_map_key()->set_int32_value(3);
  qvm.add_map_value()->set_string_value("c");

  CQLQueryParameters params;
  params.PushBackInt32("h", 1);
  params.PushBackInt32("r", 1);
  params.PushBack("vm", qvm, QLType::CreateTypeMap(DataType::INT32, DataType::STRING));

  EXPECT_OK(processor->Run(insert_vm, params));

  // Bind and execute the prepared statement with NULL in value.
  QLValue qvm_null_val;
  qvm_null_val.add_map_key()->set_int32_value(1);
  qvm_null_val.add_map_value()->Clear(); // Set to null by clearing all existing values.

  params.Reset();
  params.PushBackInt32("h", 2);
  params.PushBackInt32("r", 2);
  params.PushBack("vm", qvm_null_val, QLType::CreateTypeMap(DataType::INT32, DataType::STRING));

  Status s = processor->Run(insert_vm, params);
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::INVALID_ARGUMENTS)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(s.message().ToBuffer().find("null is not supported inside collections"),
      string::npos) << s;

  // Bind and execute the prepared statement with NULL in key.
  QLValue qvm_null_key;
  qvm_null_key.add_map_key()->Clear(); // Set to null by clearing all existing values.
  qvm_null_key.add_map_value()->set_string_value("a");

  params.Reset();
  params.PushBackInt32("h", 9);
  params.PushBackInt32("r", 9);
  params.PushBack("vm", qvm_null_key, QLType::CreateTypeMap(DataType::INT32, DataType::STRING));

  s = processor->Run(insert_vm, params);
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::INVALID_ARGUMENTS)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(s.message().ToBuffer().find("null is not supported inside collections"),
      string::npos) << s;

  //----------------------------------------------------------------------------------------------
  // Testing Set.
  //----------------------------------------------------------------------------------------------
  // Test INSERT into SET prepared statement.
  LOG(INFO) << "Prepare insert into SET statement.";
  Statement insert_vs(processor->CurrentKeyspace(),
                      "INSERT INTO test_tbl (h, r, vs) VALUES(?, ?, ?);");
  EXPECT_OK(insert_vs.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  auto vs_binds = result->bind_variable_schemas();
  EXPECT_EQ(vs_binds.size(), 3);
  EXPECT_EQ(vs_binds[0].ToString(), "h[int32 NOT NULL VALUE]");
  EXPECT_EQ(vs_binds[1].ToString(), "r[int32 NOT NULL VALUE]");
  EXPECT_EQ(vs_binds[2].ToString(), "vs[set NOT NULL VALUE]");
  EXPECT_EQ(vs_binds[2].type()->ToString(), "set<int>");

  // Bind and execute the prepared statement with correct values.
  QLValue qvs;
  qvs.add_set_elem()->set_int32_value(4);
  qvs.add_set_elem()->set_int32_value(5);

  params.Reset();
  params.PushBackInt32("h", 1);
  params.PushBackInt32("r", 1);
  params.PushBack("vs", qvs, QLType::CreateTypeSet(DataType::INT32));

  EXPECT_OK(processor->Run(insert_vs, params));

  // Bind and execute the prepared statement with NULL in SET element.
  QLValue qvs_null_elem;
  qvs_null_elem.add_set_elem()->Clear(); // Set to null by clearing all existing values.

  params.Reset();
  params.PushBackInt32("h", 9);
  params.PushBackInt32("r", 9);
  params.PushBack("vs", qvs_null_elem, QLType::CreateTypeSet(DataType::INT32));

  s = processor->Run(insert_vs, params);
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::INVALID_ARGUMENTS)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(s.message().ToBuffer().find("null is not supported inside collections"),
      string::npos) << s;

  //----------------------------------------------------------------------------------------------
  // Testing List.
  //----------------------------------------------------------------------------------------------
  // Test INSERT into LIST prepared statement.
  LOG(INFO) << "Prepare insert into LIST statement.";
  Statement insert_vl(processor->CurrentKeyspace(),
                      "INSERT INTO test_tbl (h, r, vl) VALUES(?, ?, ?);");
  EXPECT_OK(insert_vl.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  auto vl_binds = result->bind_variable_schemas();
  EXPECT_EQ(vl_binds.size(), 3);
  EXPECT_EQ(vl_binds[0].ToString(), "h[int32 NOT NULL VALUE]");
  EXPECT_EQ(vl_binds[1].ToString(), "r[int32 NOT NULL VALUE]");
  EXPECT_EQ(vl_binds[2].ToString(), "vl[list NOT NULL VALUE]");
  EXPECT_EQ(vl_binds[2].type()->ToString(), "list<text>");

  // Bind and execute the prepared statement with correct values.
  QLValue qvl;
  qvl.add_list_elem()->set_string_value("x");
  qvl.add_list_elem()->set_string_value("y");

  params.Reset();
  params.PushBackInt32("h", 1);
  params.PushBackInt32("r", 1);
  params.PushBack("vl", qvl, QLType::CreateTypeList(DataType::STRING));

  EXPECT_OK(processor->Run(insert_vl, params));

  // Bind and execute the prepared statement with NULL in LIST element.
  QLValue qvl_null_elem;
  qvl_null_elem.add_list_elem()->Clear(); // Set to null by clearing all existing values.

  params.Reset();
  params.PushBackInt32("h", 3);
  params.PushBackInt32("r", 3);
  params.PushBack("vl", qvl_null_elem, QLType::CreateTypeList(DataType::STRING));

  s = processor->Run(insert_vl, params);
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::INVALID_ARGUMENTS)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(s.message().ToBuffer().find("null is not supported inside collections"),
      string::npos) << s;

  //----------------------------------------------------------------------------------------------
  // Checking row.
  //----------------------------------------------------------------------------------------------
  CheckSelectedRow(processor, "SELECT * FROM test_tbl WHERE h = 1 AND r = 1",
      "{ int32:1, int32:1, map:{int32:2 -> string:\"b\", int32:3 -> string:\"c\"}, "
      "set:{int32:4, int32:5}, list:[string:\"x\", string:\"y\"] }");
  LOG(INFO) << "Done.";
}

TEST_F(QLTestSelectedExpr, MultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND (r1, r2) in ((5, 6))");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, MultiArgumentMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND (r1, r2) in ((5, 6), (5, 7))");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);

    row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 7);
    CHECK_EQ(row.column(3).int32_value(), 7);
  }
}

TEST_F(QLTestSelectedExpr, InvalidArgMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  Status s =
      processor->Run("SELECT * FROM test_range WHERE h = 5 AND (r1, r2) in ((5, 6, 8), (5, 7))");
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::INVALID_ARGUMENTS)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(
      s.message().ToBuffer().find("Expected 2 elements in value tuple, but got 3"), string::npos)
      << s;
}

TEST_F(QLTestSelectedExpr, EmptyArgMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND (r1, r2) in ()");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 0);
}

TEST_F(QLTestSelectedExpr, InvalidColumnOrderMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, r3 int, PRIMARY KEY ((h), r1, r2, r3));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, r3) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  Status s =
      processor->Run("SELECT * FROM test_range WHERE h = 5 AND (r1, r3) in ((5, 6), (5, 7))");
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::CQL_STATEMENT_INVALID)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(
      s.message().ToBuffer().find(
          "Clustering columns must appear in the PRIMARY KEY order in multi-column relations"),
      string::npos)
      << s;
}

TEST_F(QLTestSelectedExpr, RepeatingColumMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, r3 int, PRIMARY KEY ((h), r1, r2, r3));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, r3) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  Status s =
      processor->Run("SELECT * FROM test_range WHERE h = 5 AND (r1, r1) in ((5, 6), (5, 7))");
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::CQL_STATEMENT_INVALID)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(s.message().ToBuffer().find("Column \"r1\" appeared twice in a relation"), string::npos)
      << s;
}

TEST_F(QLTestSelectedExpr, NonRangeColMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  Status s =
      processor->Run("SELECT * FROM test_range WHERE h = 5 AND (r1, payload) in ((5, 6), (5, 7))");
  LOG(INFO) << "Expected error: " << s;
  EXPECT_TRUE(s.IsQLError()) << "Expected QLError, got: " << s;
  EXPECT_EQ(GetErrorCode(s), ErrorCode::CQL_STATEMENT_INVALID)
      << "Expected INVALID_ARGUMENT, got " << s;
  EXPECT_NE(
      s.message().ToBuffer().find("Multi-column relations can only be applied to clustering "
                                  "columns but was applied to: payload"),
      string::npos)
      << s;
}

TEST_F(QLTestSelectedExpr, OrderMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 text, payload int, PRIMARY KEY ((h), r1, r2)) "
      "WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC);";
  CHECK_VALID_STMT(create_stmt);

  for (int h = 0; h < 10; h++) {
    for (int r1 = 0; r1 < 10; r1++) {
      for (int r2 = 0; r2 < 10; r2++) {
        int v = h * 100 + r1 * 10 + r2;
        CHECK_VALID_STMT(strings::Substitute(
            "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, '$2', $3);", h, r1 * 10,
            r2 * 10, v));
      }
    }
  }

  // Checking Row
  CHECK_VALID_STMT(
      "SELECT * FROM test_range WHERE h = 1 AND (r1, r2) IN ((60, '70'), (80, '30'), (10, "
      "'70'), (60, '60'), (80, '10'), (50, '40'), (10, '80'), (10, '0'))");
  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 8);
  {
    auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 80);
    CHECK_EQ(row.column(2).string_value(), "10");
    CHECK_EQ(row.column(3).int32_value(), 181);

    row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 80);
    CHECK_EQ(row.column(2).string_value(), "30");
    CHECK_EQ(row.column(3).int32_value(), 183);

    row = row_block->row(2);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 60);
    CHECK_EQ(row.column(2).string_value(), "60");
    CHECK_EQ(row.column(3).int32_value(), 166);

    row = row_block->row(3);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 60);
    CHECK_EQ(row.column(2).string_value(), "70");
    CHECK_EQ(row.column(3).int32_value(), 167);

    row = row_block->row(4);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 50);
    CHECK_EQ(row.column(2).string_value(), "40");
    CHECK_EQ(row.column(3).int32_value(), 154);

    row = row_block->row(5);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 10);
    CHECK_EQ(row.column(2).string_value(), "0");
    CHECK_EQ(row.column(3).int32_value(), 110);

    row = row_block->row(6);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 10);
    CHECK_EQ(row.column(2).string_value(), "70");
    CHECK_EQ(row.column(3).int32_value(), 117);

    row = row_block->row(7);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 10);
    CHECK_EQ(row.column(2).string_value(), "80");
    CHECK_EQ(row.column(3).int32_value(), 118);
  }

  // With ORDER BY clause (reverse scan)
  CHECK_VALID_STMT(
      "SELECT * FROM test_range WHERE h = 1 AND (r1, r2) IN ((60, '70'), (80, '30'), (10, "
      "'70'), (60, '60'), (80, '10'), (50, '40'), (10, '80'), (10, '0')) ORDER BY r1 ASC, r2 DESC");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 8);
  {
    auto& row = row_block->row(7);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 80);
    CHECK_EQ(row.column(2).string_value(), "10");
    CHECK_EQ(row.column(3).int32_value(), 181);

    row = row_block->row(6);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 80);
    CHECK_EQ(row.column(2).string_value(), "30");
    CHECK_EQ(row.column(3).int32_value(), 183);

    row = row_block->row(5);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 60);
    CHECK_EQ(row.column(2).string_value(), "60");
    CHECK_EQ(row.column(3).int32_value(), 166);

    row = row_block->row(4);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 60);
    CHECK_EQ(row.column(2).string_value(), "70");
    CHECK_EQ(row.column(3).int32_value(), 167);

    row = row_block->row(3);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 50);
    CHECK_EQ(row.column(2).string_value(), "40");
    CHECK_EQ(row.column(3).int32_value(), 154);

    row = row_block->row(2);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 10);
    CHECK_EQ(row.column(2).string_value(), "0");
    CHECK_EQ(row.column(3).int32_value(), 110);

    row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 10);
    CHECK_EQ(row.column(2).string_value(), "70");
    CHECK_EQ(row.column(3).int32_value(), 117);

    row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).int32_value(), 10);
    CHECK_EQ(row.column(2).string_value(), "80");
    CHECK_EQ(row.column(3).int32_value(), 118);
  }
}

TEST_F(QLTestSelectedExpr, RandomizedMultiColumnInTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 text, r3 int, PRIMARY KEY ((h), r1, r2, r3)) "
      "WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC, r3 DESC);";
  CHECK_VALID_STMT(create_stmt);

  int num_rows = 5000;
  int limit = 10;
  for (int i = 0; i < num_rows; i++) {
    unsigned int seed = SeedRandom();
    int h = rand_r(&seed) % limit;
    int r1 = rand_r(&seed) % limit;
    int r2 = rand_r(&seed) % limit;
    int r3 = rand_r(&seed) % limit;
    CHECK_VALID_STMT(strings::Substitute(
        "INSERT INTO test_range (h, r1, r2, r3) VALUES($0, $1, '$2', $3);", h, r1, r2, r3));
  }

  // Checking Row
  int queries = 1000;
  for (int i = 0; i < queries; i++) {
    unsigned int seed = SeedRandom();
    std::this_thread::sleep_for(10ms);
    int h = rand_r(&seed) % limit;
    std::unordered_set<int> params;
    int r1_1 = 0;
    int r2_1 = 0;
    int r3_1 = 0;
    int r1_2 = 0;
    int r2_2 = 0;
    int r3_2 = 0;
    int r1_3 = 0;
    int r2_3 = 0;
    int r3_3 = 0;
    while (params.size() != 3) {
      params.erase(r1_1 * 100 + r2_1 * 10 + r3_1);
      params.erase(r1_2 * 100 + r2_2 * 10 + r3_2);
      params.erase(r1_3 * 100 + r2_3 * 10 + r3_3);
      r1_1 = rand_r(&seed) % limit;
      r2_1 = rand_r(&seed) % limit;
      r3_1 = rand_r(&seed) % limit;
      r1_2 = rand_r(&seed) % limit;
      r2_2 = rand_r(&seed) % limit;
      r3_2 = rand_r(&seed) % limit;
      r1_3 = rand_r(&seed) % limit;
      r2_3 = rand_r(&seed) % limit;
      r3_3 = rand_r(&seed) % limit;
      params.insert(r1_1 * 100 + r2_1 * 10 + r3_1);
      params.insert(r1_2 * 100 + r2_2 * 10 + r3_2);
      params.insert(r1_3 * 100 + r2_3 * 10 + r3_3);
    }

    CHECK_VALID_STMT(strings::Substitute(
        "SELECT * FROM test_range WHERE h = $0 AND (r1, r2, r3) IN (($1, '$2', $3), ($4, '$5', "
        "$6), ($7, '$8', $9));",
        h, r1_1, r2_1, r3_1, r1_2, r2_2, r3_2, r1_3, r2_3, r3_3));
    QLRowBlock in_rows = *processor->row_block();
    size_t in_rows_count = in_rows.row_count();
    std::unordered_set<int> ins;
    for (auto const& row : in_rows.rows()) {
      int h = row.column(0).int32_value();
      int r1 = row.column(1).int32_value();
      string r2 = row.column(2).string_value();
      int r3 = row.column(3).int32_value();
      int value = h * 1000 + r1 * 100 + stoi(r2) * 10 + r3;
      ins.insert(value);
    }

    CHECK_VALID_STMT(strings::Substitute(
        "SELECT * FROM test_range WHERE h = $0 AND r1 = $1 AND r2 = '$2' AND r3 = $3;", h, r1_1,
        r2_1, r3_1));

    QLRowBlock alt_rows = *processor->row_block();
    size_t alt_rows_count = alt_rows.row_count();
    for (auto const& row : alt_rows.rows()) {
      int h = row.column(0).int32_value();
      int r1 = row.column(1).int32_value();
      string r2 = row.column(2).string_value();
      int r3 = row.column(3).int32_value();
      int value = h * 1000 + r1 * 100 + stoi(r2) * 10 + r3;
      ins.erase(value);
    }

    CHECK_VALID_STMT(strings::Substitute(
        "SELECT * FROM test_range WHERE h = $0 AND r1 = $1 AND r2 = '$2' AND r3 = $3;", h, r1_2,
        r2_2, r3_2));

    alt_rows = *processor->row_block();
    alt_rows_count += alt_rows.row_count();
    for (auto const& row : alt_rows.rows()) {
      int h = row.column(0).int32_value();
      int r1 = row.column(1).int32_value();
      string r2 = row.column(2).string_value();
      int r3 = row.column(3).int32_value();
      int value = h * 1000 + r1 * 100 + stoi(r2) * 10 + r3;
      ins.erase(value);
    }

    CHECK_VALID_STMT(strings::Substitute(
        "SELECT * FROM test_range WHERE h = $0 AND r1 = $1 AND r2 = '$2' AND r3 = $3;", h, r1_3,
        r2_3, r3_3));

    alt_rows = *processor->row_block();
    alt_rows_count += alt_rows.row_count();
    for (auto const& row : alt_rows.rows()) {
      int h = row.column(0).int32_value();
      int r1 = row.column(1).int32_value();
      string r2 = row.column(2).string_value();
      int r3 = row.column(3).int32_value();
      int value = h * 1000 + r1 * 100 + stoi(r2) * 10 + r3;
      ins.erase(value);
    }
    CHECK_EQ(in_rows_count, alt_rows_count);
    CHECK_EQ(ins.size(), 0);
  }
}

TEST_F(QLTestSelectedExpr, BindVarsMultiColumnInTest) {
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  PreparedResult::UniPtr result;
  LOG(INFO) << "Prepare insert into LIST statement.";
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 text, r3 int, PRIMARY KEY ((h), r1, r2, r3)) "
      "WITH CLUSTERING ORDER BY (r1 DESC, r2 ASC, r3 DESC);";
  CHECK_VALID_STMT(create_stmt);

  Statement select_statement(
      processor->CurrentKeyspace(),
      "SELECT * FROM test_range WHERE h = 5 AND (r1, r2) IN (:tup1, :tup2);");
  EXPECT_OK(select_statement.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  auto vl_binds = result->bind_variable_schemas();
  EXPECT_EQ(vl_binds.size(), 2);
  EXPECT_EQ(vl_binds[0].ToString(), "tup1[tuple NOT NULL VALUE]");
  EXPECT_EQ(vl_binds[1].ToString(), "tup2[tuple NOT NULL VALUE]");
  EXPECT_EQ(vl_binds[1].type()->ToString(), "tuple<int, text>");
}

TEST_F(QLTestSelectedExpr, TestPreparedStatementWithEmbeddedNull) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  PreparedResult::UniPtr result;

  LOG(INFO) << "Create and setup test table.";
  CHECK_VALID_STMT("CREATE TABLE test_tbl (h TEXT, r TEXT, v1 TEXT, v2 INT, PRIMARY KEY((h), r))");

  // Test INSERT into the prepared statement.
  LOG(INFO) << "Prepare insert into statement.";
  Statement insert_statement(processor->CurrentKeyspace(),
                             "INSERT INTO test_tbl (h, r, v1, v2) VALUES(?, ?, ?, ?);");
  EXPECT_OK(insert_statement.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  auto binds = result->bind_variable_schemas();
  EXPECT_EQ(binds.size(), 4);
  EXPECT_EQ(binds[0].ToString(), "h[string NOT NULL VALUE]");
  EXPECT_EQ(binds[1].ToString(), "r[string NOT NULL VALUE]");
  EXPECT_EQ(binds[2].ToString(), "v1[string NOT NULL VALUE]");
  EXPECT_EQ(binds[3].ToString(), "v2[int32 NOT NULL VALUE]");

  // Bind and execute the prepared statement with values where the text column value
  // embedded null byte.
  CQLQueryParameters params1;
  string null_embedding_str1("\0a", 2);
  params1.PushBackString("h", null_embedding_str1);
  params1.PushBackString("r", null_embedding_str1);
  params1.PushBackString("v1", null_embedding_str1);
  params1.PushBackInt32("v2", 1);
  CQLQueryParameters params2;
  string null_embedding_str2("a\0", 2);
  params2.PushBackString("h", null_embedding_str2);
  params2.PushBackString("r", null_embedding_str2);
  params2.PushBackString("v1", null_embedding_str2);
  params2.PushBackInt32("v2", 2);
  CQLQueryParameters params3;
  string null_embedding_str3("a\0b", 3);
  params3.PushBackString("h", null_embedding_str3);
  params3.PushBackString("r", null_embedding_str3);
  params3.PushBackString("v1", null_embedding_str3);
  params3.PushBackInt32("v2", 3);

  EXPECT_OK(processor->Run(insert_statement, params1));
  EXPECT_OK(processor->Run(insert_statement, params2));
  EXPECT_OK(processor->Run(insert_statement, params3));

  //----------------------------------------------------------------------------------------------
  // Checking rows.
  //----------------------------------------------------------------------------------------------
  CheckSelectedRow(processor, "SELECT * FROM test_tbl WHERE v2 = 1",
      "{ string:\"\\x00a\", string:\"\\x00a\", string:\"\\x00a\", int32:1 }");
  CheckSelectedRow(processor, "SELECT * FROM test_tbl WHERE v2 = 2",
      "{ string:\"a\\x00\", string:\"a\\x00\", string:\"a\\x00\", int32:2 }");
  CheckSelectedRow(processor, "SELECT * FROM test_tbl WHERE v2 = 3",
      "{ string:\"a\\x00b\", string:\"a\\x00b\", string:\"a\\x00b\", int32:3 }");
  LOG(INFO) << "Done.";
}

TEST_F(QLTestSelectedExpr, MapMultiFieldQueryTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  // Create the table 1.
  const char* create_stmt = "CREATE TABLE users(username TEXT PRIMARY KEY, phones MAP<TEXT,TEXT>);";
  CHECK_VALID_STMT(create_stmt);

  CHECK_VALID_STMT(
      "INSERT INTO users(username, phones) VALUES ('foo', {'home' : '999-9999', 'mobile' : "
      "'000-0000', 'work':'222-222'});");

  // Checking Row
  CHECK_VALID_STMT("SELECT username, phones['work'], phones['home'] from users;");

  auto row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).string_value(), "foo");
    CHECK_EQ(row.column(1).string_value(), "222-222");
    CHECK_EQ(row.column(2).string_value(), "999-9999");
  }

  // With alias
  CHECK_VALID_STMT("SELECT username, phones['work'] as work, phones['home'] as home from users;");

  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  {
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).string_value(), "foo");
    CHECK_EQ(row.column(1).string_value(), "222-222");
    CHECK_EQ(row.column(2).string_value(), "999-9999");
  }
}

} // namespace ql
} // namespace yb
