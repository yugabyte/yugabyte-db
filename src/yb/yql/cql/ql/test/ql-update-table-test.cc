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

#include "yb/gutil/strings/substitute.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;
using strings::Substitute;

namespace yb {
namespace ql {

class TestQLUpdateTable : public QLTestBase {
 public:
  TestQLUpdateTable() : QLTestBase() {
  }

  std::string GetUpdateStmt(int64_t ttl_msec) {
    return strings::Substitute(
        "UPDATE test_table USING TTL $0 SET v1 = 1 WHERE h1 = 0 AND h2 = 'zero' AND r1 = 1 "
            "AND r2 = 'r2';", ttl_msec);
  }
};

TEST_F(TestQLUpdateTable, TestQLUpdateToJson) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  std::shared_ptr<qlexpr::QLRowBlock> row_block;

  auto to_json_str = [](const QLValue& value) -> string {
    common::Jsonb jsonb(value.jsonb_value());
    string str;
    CHECK_OK(jsonb.ToJsonString(&str));
    return str;
  };

  // Create table with JSONB.
  CHECK_VALID_STMT("CREATE TABLE test_json (h int PRIMARY KEY, j jsonb)");

  CHECK_VALID_STMT("INSERT INTO test_json (h, j) values (1, '{\"a\":123}')");
  CHECK_VALID_STMT("UPDATE test_json SET j->'a' = '{}', j->'a'->'b' = '6' where h = 1");
  CHECK_VALID_STMT("SELECT * FROM test_json");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(1, row_block->row(0).column(0).int32_value());
  EXPECT_EQ("{\"a\":{\"b\":6}}", to_json_str(row_block->row(0).column(1)));
}

TEST_F(TestQLUpdateTable, TestQLUpdateTableSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // -----------------------------------------------------------------------------------------------
  // Create the table.
  const char *create_stmt =
    "CREATE TABLE test_table(h1 int, h2 varchar, "
                            "r1 int, r2 varchar, "
                            "v1 int, v2 varchar, "
                            "primary key((h1, h2), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  CHECK_VALID_STMT(GetUpdateStmt(yb::common::kCassandraMaxTtlSeconds));
  CHECK_VALID_STMT(GetUpdateStmt(yb::common::kCassandraMinTtlSeconds));
  CHECK_INVALID_STMT(GetUpdateStmt(yb::common::kCassandraMaxTtlSeconds + 1));
  CHECK_INVALID_STMT(GetUpdateStmt(yb::common::kCassandraMinTtlSeconds - 1));

  // -----------------------------------------------------------------------------------------------
  // Unknown table.
  CHECK_INVALID_STMT("UPDATE test_table_unknown SET v1 = 77 WHERE h1 = 0 AND h2 = 'zero';");

  // Missing hash key.
  CHECK_INVALID_STMT("UPDATE test_table SET v1 = 77 WHERE h1 = 0;");

  // Wrong operator on hash key.
  CHECK_INVALID_STMT("UPDATE test_table SET v1 = 77 WHERE h1 > 0 AND h2 = 'zero';");

  // -----------------------------------------------------------------------------------------------
  // Insert 100 rows into the table.
  static const int kNumRows = 100;
  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, 'h$1', $2, 'r$3', $4, 'v$5');",
                             idx, idx, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }

  // Testing UPDATE one row.
  string select_stmt;
  auto row_block = processor->row_block();
  for (int idx = 0; idx < kNumRows; idx++) {
    // SELECT an entry to make sure it's there.
    select_stmt = Substitute("SELECT * FROM test_table"
                             "  WHERE h1 = $0 AND h2 = 'h$1' AND r1 = $2 AND r2 = 'r$3';",
                             idx, idx, idx+100, idx+100);
    CHECK_VALID_STMT(select_stmt);
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);

    // UPDATE the entry.
    CHECK_VALID_STMT(Substitute("UPDATE test_table SET v1 = $0, v2 = 'v$0'"
                                "  WHERE h1 = $1 AND h2 = 'h$1' AND r1 = $2 AND r2 = 'r$2';",
                                idx + 2000, idx, idx+100));

    // SELECT the same entry to make sure it's no longer there.
    CHECK_VALID_STMT(select_stmt);
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), idx);
    CHECK_EQ(row.column(1).string_value(), Substitute("h$0", idx));
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 2000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 2000));
  }
}

} // namespace ql
} // namespace yb
