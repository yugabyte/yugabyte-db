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

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;
using std::shared_ptr;

namespace yb {
namespace ql {

class TestQLStaticColumn : public QLTestBase {
 public:
  TestQLStaticColumn() : QLTestBase() {
  }

  std::shared_ptr<qlexpr::QLRowBlock> ExecSelect(
      TestQLProcessor *processor, int expected_rows = 1) {
    auto select = "SELECT c1, c2, c3 FROM test_table WHERE c1 = 1";
    Status s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(expected_rows, row_block->row_count());
    return row_block;
  }

  void VerifyExpiry(TestQLProcessor *processor) {
    ExecSelect(processor, 0);
  }

  // Create test table.
  void CreateTable(TestQLProcessor *processor) {
    CHECK_VALID_STMT("create table t ("
                     " h1 int, h2 varchar,"
                     " r1 int, r2 varchar,"
                     " s1 int static, s2 varchar static,"
                     " c1 int, c2 varchar,"
                     " primary key ((h1, h2), r1, r2));");
  }

};

TEST_F(TestQLStaticColumn, TestCreateTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  EXEC_VALID_STMT("create table static_table (h int, r int, s int static, PRIMARY KEY((h), r));");

  // Query the table schema.
  auto &catalog_manager = cluster_->mini_master()->catalog_manager();
  master::GetTableSchemaRequestPB request_pb;
  master::GetTableSchemaResponsePB response_pb;
  request_pb.mutable_table()->mutable_namespace_()->set_name(kDefaultKeyspaceName);
  request_pb.mutable_table()->set_table_name("static_table");

  // Verify the static column.
  CHECK_OK(catalog_manager.GetTableSchema(&request_pb, &response_pb));
  for (const ColumnSchemaPB& column : response_pb.schema().columns()) {
    if (column.name() == "s") {
      EXPECT_TRUE(column.is_static());
      break;
    }
  }
}

TEST_F(TestQLStaticColumn, TestSelect) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  LOG(INFO) << "Running TestSelect";

  // Create test table.
  CreateTable(processor);

  // Insert 2 rows while updating static columns in the 2nd insert.
  CHECK_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, s1, s2, c1, c2) "
                   "VALUES (1, 'a', 1, 'a', 1, 'a', 11, 'aa');");
  CHECK_VALID_STMT("INSERT INTO t (h1, h2, r1, r2, s1, s2, c1, c2) "
                   "VALUES (1, 'a', 2, 'b', 2, 'b', 22, 'bb');");

  // Test simple query and result.
  {
    CHECK_VALID_STMT("SELECT h1, h2, r1, r2, s1, s2, c1, c2 FROM t WHERE h1 = 1 AND h2 = 'a';");
    auto row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const auto& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 1);
    CHECK_EQ(row1.column(1).string_value(), "a");
    CHECK_EQ(row1.column(2).int32_value(), 1);
    CHECK_EQ(row1.column(3).string_value(), "a");
    CHECK_EQ(row1.column(4).int32_value(), 2);
    CHECK_EQ(row1.column(5).string_value(), "b");
    CHECK_EQ(row1.column(6).int32_value(), 11);
    CHECK_EQ(row1.column(7).string_value(), "aa");
    const auto& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 1);
    CHECK_EQ(row2.column(1).string_value(), "a");
    CHECK_EQ(row2.column(2).int32_value(), 2);
    CHECK_EQ(row2.column(3).string_value(), "b");
    CHECK_EQ(row2.column(4).int32_value(), 2);
    CHECK_EQ(row2.column(5).string_value(), "b");
    CHECK_EQ(row2.column(6).int32_value(), 22);
    CHECK_EQ(row2.column(7).string_value(), "bb");
  }

  // Test select distinct.
  {
    CHECK_VALID_STMT("SELECT DISTINCT h1, h2, s1, s2 FROM t WHERE h1 = 1 AND h2 = 'a';");
    auto row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const auto& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 1);
    CHECK_EQ(row.column(1).string_value(), "a");
    CHECK_EQ(row.column(2).int32_value(), 2);
    CHECK_EQ(row.column(3).string_value(), "b");
  }

  // Insert static columns only.
  CHECK_VALID_STMT("INSERT INTO t (h1, h2, s1, s2) VALUES (1, 'a', 3, 'c');");
  CHECK_VALID_STMT("INSERT INTO t (h1, h2, s1, s2) VALUES (2, 'b', 4, 'd');");

  // Test simple query and result.
  {
    CHECK_VALID_STMT("SELECT s1, s2, c1, c2 FROM t WHERE h1 = 1 AND h2 = 'a';");
    auto row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const auto& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 3);
    CHECK_EQ(row1.column(1).string_value(), "c");
    CHECK_EQ(row1.column(2).int32_value(), 11);
    CHECK_EQ(row1.column(3).string_value(), "aa");
    const auto& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 3);
    CHECK_EQ(row2.column(1).string_value(), "c");
    CHECK_EQ(row2.column(2).int32_value(), 22);
    CHECK_EQ(row2.column(3).string_value(), "bb");
  }

  // Read all static rows. Verify rows and that they are read in the number of pages expected.
  {
    CHECK_OK(processor->Run("SELECT DISTINCT s1, s2 FROM t;"));
    auto row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const auto& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 3);
    CHECK_EQ(row1.column(1).string_value(), "c");
    const auto& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 4);
    CHECK_EQ(row2.column(1).string_value(), "d");
  }

  // Update static and non-static columns together.
  CHECK_VALID_STMT("UPDATE t set s1 = 5, s2 = 'e', c1 = 33, c2 = 'cc' "
                   "WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b';");
  // Update static columns only.
  CHECK_VALID_STMT("UPDATE t set s1 = 6, s2 = 'f' WHERE h1 = 2 AND h2 = 'b';");

  // Test simple query and result.
  {
    CHECK_VALID_STMT("SELECT s1, s2, c1, c2 FROM t WHERE h1 = 1 AND h2 = 'a';");
    auto row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const auto& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 5);
    CHECK_EQ(row1.column(1).string_value(), "e");
    CHECK_EQ(row1.column(2).int32_value(), 11);
    CHECK_EQ(row1.column(3).string_value(), "aa");
    const auto& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 5);
    CHECK_EQ(row2.column(1).string_value(), "e");
    CHECK_EQ(row2.column(2).int32_value(), 33);
    CHECK_EQ(row2.column(3).string_value(), "cc");
  }

  // Read all static rows. Verify rows and that they are read in the number of pages expected.
  {
    CHECK_OK(processor->Run("SELECT DISTINCT s1, s2 FROM t;"));
    auto row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const auto& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 5);
    CHECK_EQ(row1.column(1).string_value(), "e");
    const auto& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 6);
    CHECK_EQ(row2.column(1).string_value(), "f");
  }
}

TEST_F(TestQLStaticColumn, TestPagingSelect) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  LOG(INFO) << "Running TestSelect";

  // Create test table.
  CHECK_VALID_STMT("CREATE TABLE t (h int, r int, s int static, c int, PRIMARY KEY ((h), r));");

  // Insert 6 rows with 2 different hash keys.
  CHECK_VALID_STMT("INSERT INTO t (h, r, s, c) VALUES (1, 1, 1, 11);");
  CHECK_VALID_STMT("INSERT INTO t (h, r, s, c) VALUES (1, 2, 2, 12);");
  CHECK_VALID_STMT("INSERT INTO t (h, r, s, c) VALUES (1, 3, 3, 13);");

  CHECK_VALID_STMT("INSERT INTO t (h, r, s, c) VALUES (2, 1, 4, 21);");
  CHECK_VALID_STMT("INSERT INTO t (h, r, s, c) VALUES (2, 2, 5, 22);");
  CHECK_VALID_STMT("INSERT INTO t (h, r, s, c) VALUES (2, 3, 6, 23);");

  // Test selecting all rows and columns. Ensure the static column is not missed across pages.
  VerifyPaginationSelect(processor, "SELECT * FROM t;", 1,
      "{ { int32:1, int32:1, int32:3, int32:11 } }"
      "{ { int32:1, int32:2, int32:3, int32:12 } }"
      "{ { int32:1, int32:3, int32:3, int32:13 } }"
      "{ { int32:2, int32:1, int32:6, int32:21 } }"
      "{ { int32:2, int32:2, int32:6, int32:22 } }"
      "{ { int32:2, int32:3, int32:6, int32:23 } }");

  // Test selecting all rows for the non-primary key columns using page-size 2. Ensure the static
  // column is not missed across pages also.
  VerifyPaginationSelect(processor, "SELECT s, c FROM t;", 2,
      "{ { int32:3, int32:11 }, { int32:3, int32:12 } }"
      "{ { int32:3, int32:13 }, { int32:6, int32:21 } }"
      "{ { int32:6, int32:22 }, { int32:6, int32:23 } }");

  // Test selecting all distinct static columns with page-size 1.
  VerifyPaginationSelect(processor, "SELECT DISTINCT s FROM t;", 1,
      "{ { int32:3 } }"
      "{ { int32:6 } }");
}

} // namespace ql
} // namespace yb
