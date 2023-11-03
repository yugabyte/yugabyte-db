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

#include "yb/common/jsonb.h"
#include "yb/common/ql_value.h"
#include "yb/common/table_properties_constants.h"

#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;

namespace yb {
namespace ql {

class TestQLInsertTable : public QLTestBase {
 public:
  TestQLInsertTable() : QLTestBase() {
  }

  std::string InsertStmtWithTTL(std::string ttl_seconds) {
    return strings::Substitute(
        "INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100) USING TTL $0;",
        ttl_seconds);
  }
};

TEST_F(TestQLInsertTable, TestQLInsertTableSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table 1.
  EXEC_VALID_STMT(
      "CREATE TABLE human_resource(id int, name varchar, salary int, primary key(id, name));");

  // INSERT: Valid statement with column list.
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100);");
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(-1, 'Scott Tiger', -100);");

  // INSERT: Valid statement with column list and ttl.
  EXEC_VALID_STMT(InsertStmtWithTTL(std::to_string(1)));

  // INSERT: Valid statement with ttl at the lower limit.
  EXEC_VALID_STMT(InsertStmtWithTTL(std::to_string(yb::common::kCassandraMinTtlSeconds)));

  // INSERT: Valid statement with ttl at the upper limit.
  EXEC_VALID_STMT(InsertStmtWithTTL(std::to_string(yb::common::kCassandraMaxTtlSeconds)));

  // INSERT: Invalid statement with ttl just over limit.
  EXEC_INVALID_STMT(InsertStmtWithTTL(std::to_string(yb::common::kCassandraMaxTtlSeconds + 1)));

  // INSERT: Invalid statement with ttl just below lower limit.
  EXEC_INVALID_STMT(InsertStmtWithTTL(std::to_string(yb::common::kCassandraMinTtlSeconds - 1)));

  // INSERT: Invalid statement with ttl too high.
  EXEC_INVALID_STMT(InsertStmtWithTTL(std::to_string(std::numeric_limits<int64_t>::max())));

  // INSERT: Invalid statement with float ttl.
  EXEC_INVALID_STMT(InsertStmtWithTTL("1.1"));

  // INSERT: Invalid statement with negative ttl.
  EXEC_INVALID_STMT(InsertStmtWithTTL(std::to_string(-1)));

  // INSERT: Invalid statement with hex ttl.
  EXEC_INVALID_STMT(InsertStmtWithTTL("0x100"));

  // INSERT: Statement with invalid TTL syntax.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100) "
      "TTL 1000;");
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(-1, 'Scott Tiger', -100) "
      "TTL 1000;");

  // INSERT: Statement with invalid TTL value.
  EXEC_INVALID_STMT(InsertStmtWithTTL("abcxyz"));

  // INSERT: Invalid CQL statement though it's a valid SQL statement without column list.
  EXEC_INVALID_STMT("INSERT INTO human_resource VALUES(2, 'Scott Tiger', 100);");
  EXEC_INVALID_STMT("INSERT INTO human_resource VALUES(-2, 'Scott Tiger', -100);");

  // INSERT: Invalid statement - Duplicate key.
  // IF NOT EXISTS is not implemented yet, so we ignore this test case for now.
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100) "
                  "IF NOT EXISTS;");
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(-1, 'Scott Tiger', -100) "
                  "IF NOT EXISTS;");

  // INSERT: Invalid statement - "--" is not yet supported.
  // TODO(neil) Parser needs to handle this error more elegantly.
  EXEC_INVALID_STMT("INSERT INTO human_resource_wrong(id, name, salary)"
                    "  VALUES(--1, 'Scott Tiger', 100);");

  // INSERT: Invalid statement - Wrong table name.
  EXEC_INVALID_STMT("INSERT INTO human_resource_wrong(id, name, salary)"
                    "  VALUES(1, 'Scott Tiger', 100);");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger');");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary)"
                    " VALUES(1, 'Scott Tiger', 100, 200);");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1);");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name) VALUES(1, 'Scott Tiger', 100);");

  // INSERT: Invalid statement - Missing primary key (id).
  EXEC_INVALID_STMT("INSERT INTO human_resource(name, salary) VALUES('Scott Tiger', 100);");

  // INSERT: Invalid statement - Missing primary key (name).
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, salary) VALUES(2, 100);");

  // Because string and numeric datatypes are implicitly compatible, we cannot test datatype
  // mismatch yet. Once timestamp, boolean, ... types are introduced, type incompability should be
  // tested here also.
}

TEST_F(TestQLInsertTable, TestQLInsertCast) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  std::shared_ptr<qlexpr::QLRowBlock> row_block;
  LOG(INFO) << "Test inserting with cast(... as text) built-in.";

  // Create table.
  CHECK_VALID_STMT("CREATE TABLE test_cast (h int PRIMARY KEY, t text)");

  // Test simple insert.
  CHECK_VALID_STMT("INSERT INTO test_cast (h, t) values (1, 'a')");
  CHECK_VALID_STMT("SELECT * FROM test_cast");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(1, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("a", row_block->row(0).column(1).string_value());

  // Test various insert vi cast() built-in.
  // cast(FLOAT as TEXT)
  CHECK_VALID_STMT("INSERT INTO test_cast (h, t) values (3, cast(1234.5 as text))");
  CHECK_VALID_STMT("SELECT * FROM test_cast where h = 3");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(3, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("1234.5", row_block->row(0).column(1).string_value());
  // cast(TEXT as TEXT)
  CHECK_VALID_STMT("INSERT INTO test_cast (h, t) values (4, cast('ABC' as text))");
  CHECK_VALID_STMT("SELECT * FROM test_cast where h = 4");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(4, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("ABC", row_block->row(0).column(1).string_value());
  // cast(TEXT as TEXT) where the string is a list in string.
  CHECK_VALID_STMT("INSERT INTO test_cast (h, t) values (5, cast('[1, 2]' as text))");
  CHECK_VALID_STMT("SELECT * FROM test_cast where h = 5");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(5, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("[1, 2]", row_block->row(0).column(1).string_value());

  // cast(INT as TEXT)
  EXEC_INVALID_STMT_WITH_ERROR("INSERT INTO test_cast (h, t) values (2, cast(22 as text))",
      "Execution Error. Cannot convert varint to text");
  // cast(LIST as TEXT)
  EXEC_INVALID_STMT_WITH_ERROR("INSERT INTO test_cast (h, t) values (7, cast([1, 2] as text))",
      "Invalid Arguments. Input argument must be of primitive type");
  // cast(SET as TEXT)
  EXEC_INVALID_STMT_WITH_ERROR("INSERT INTO test_cast (h, t) values (7, cast({1, 2} as text))",
      "Invalid Arguments. Input argument must be of primitive type");
  // cast(MAP as TEXT)
  EXEC_INVALID_STMT_WITH_ERROR(
      "INSERT INTO test_cast (h, t) values (7, cast({'a':11, 'b':22} as text))",
      "Invalid Arguments. Input argument must be of primitive type");
}

TEST_F(TestQLInsertTable, TestQLInsertToJson) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  std::shared_ptr<qlexpr::QLRowBlock> row_block;
  LOG(INFO) << "Test inserting with ToJson() built-in.";

  auto to_json_str = [](const QLValue& value) -> string {
    common::Jsonb jsonb(value.jsonb_value());
    string str;
    CHECK_OK(jsonb.ToJsonString(&str));
    return str;
  };

  // Create table with JSONB.
  CHECK_VALID_STMT("CREATE TABLE test_json (h int PRIMARY KEY, j jsonb)");

  // Test simple JSON insert.
  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (1, '{\"a\":123}')");
  CHECK_VALID_STMT("SELECT * FROM test_json");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(1, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("{\"a\":123}", to_json_str(row_block->row(0).column(1)));

  // Test various insert vi ToJson() built-in.
  // ToJson(INT)
  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (2, ToJson(22))");
  CHECK_VALID_STMT("SELECT * FROM test_json where h = 2");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(2, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("22", to_json_str(row_block->row(0).column(1)));
  // ToJson(FLOAT)
  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (3, ToJson(1234.5))");
  CHECK_VALID_STMT("SELECT * FROM test_json where h = 3");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(3, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("1234.5", to_json_str(row_block->row(0).column(1)));
  // ToJson(TEXT)
  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (4, ToJson('ABC'))");
  CHECK_VALID_STMT("SELECT * FROM test_json where h = 4");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(4, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("\"ABC\"", to_json_str(row_block->row(0).column(1)));
  // ToJson(TEXT) where the string is a list in string.
  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (5, ToJson('[1, 2]'))");
  CHECK_VALID_STMT("SELECT * FROM test_json where h = 5");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(5, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("\"[1, 2]\"", to_json_str(row_block->row(0).column(1)));
  // ToJson(TEXT) where the string is a set in string.
  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (6, ToJson('{1, 2}'))");
  CHECK_VALID_STMT("SELECT * FROM test_json where h = 6");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(6, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("\"{1, 2}\"", to_json_str(row_block->row(0).column(1)));

  // ToJson(LIST)
  EXEC_INVALID_STMT_WITH_ERROR("INSERT INTO test_json (h, j) values (7, ToJson([1, 2]))",
      "Invalid Arguments. Input argument must be of primitive type");
  // ToJson(SET)
  EXEC_INVALID_STMT_WITH_ERROR("INSERT INTO test_json (h, j) values (8, ToJson({3, 4}))",
      "Invalid Arguments. Input argument must be of primitive type");
  // ToJson(MAP)
  EXEC_INVALID_STMT_WITH_ERROR("INSERT INTO test_json (h, j) values (9, ToJson({5:55, 6:66}))",
      "Invalid Arguments. Input argument must be of primitive type");
  EXEC_INVALID_STMT_WITH_ERROR(
      "INSERT INTO test_json (h, j) values (10, ToJson({a : 1, b : 'foo'}))",
      "Invalid Arguments. Input argument must be of primitive type");
}

} // namespace ql
} // namespace yb
