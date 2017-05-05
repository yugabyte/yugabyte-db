//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using strings::Substitute;

namespace yb {
namespace sql {

class YbSqlStaticColumn : public YbSqlTestBase {
 public:
  YbSqlStaticColumn() : YbSqlTestBase() {
  }

  std::shared_ptr<YQLRowBlock> ExecSelect(YbSqlProcessor *processor, int expected_rows = 1) {
    auto select = "SELECT c1, c2, c3 FROM test_table WHERE c1 = 1";
    Status s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(expected_rows, row_block->row_count());
    return row_block;
  }

  void VerifyExpiry(YbSqlProcessor *processor) {
    ExecSelect(processor, 0);
  }

  // Create test table.
  void CreateTable(YbSqlProcessor *processor) {
    CHECK_VALID_STMT("create table t ("
                     " h1 int, h2 varchar,"
                     " r1 int, r2 varchar,"
                     " s1 int static, s2 varchar static,"
                     " c1 int, c2 varchar,"
                     " primary key ((h1, h2), r1, r2));");
  }

};

TEST_F(YbSqlStaticColumn, TestCreateTable) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  // Create the table.
  EXEC_VALID_STMT("create table static_table (h int, r int, s int static, PRIMARY KEY((h), r));");

  // Query the table schema.
  master::Master *master = cluster_->mini_master()->master();
  master::CatalogManager *catalog_manager = master->catalog_manager();
  master::GetTableSchemaRequestPB request_pb;
  master::GetTableSchemaResponsePB response_pb;
  request_pb.mutable_table()->set_table_name("static_table");

  // Verify the static column.
  CHECK_OK(catalog_manager->GetTableSchema(&request_pb, &response_pb));
  for (const ColumnSchemaPB& column : response_pb.schema().columns()) {
    if (column.name() == "s") {
      EXPECT_TRUE(column.is_static());
      break;
    }
  }
}

TEST_F(YbSqlStaticColumn, TestSelect) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

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
    std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const YQLRow& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 1);
    CHECK_EQ(row1.column(1).string_value(), "a");
    CHECK_EQ(row1.column(2).int32_value(), 1);
    CHECK_EQ(row1.column(3).string_value(), "a");
    CHECK_EQ(row1.column(4).int32_value(), 2);
    CHECK_EQ(row1.column(5).string_value(), "b");
    CHECK_EQ(row1.column(6).int32_value(), 11);
    CHECK_EQ(row1.column(7).string_value(), "aa");
    const YQLRow& row2 = row_block->row(1);
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
    std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const YQLRow& row = row_block->row(0);
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
    std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const YQLRow& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 3);
    CHECK_EQ(row1.column(1).string_value(), "c");
    CHECK_EQ(row1.column(2).int32_value(), 11);
    CHECK_EQ(row1.column(3).string_value(), "aa");
    const YQLRow& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 3);
    CHECK_EQ(row2.column(1).string_value(), "c");
    CHECK_EQ(row2.column(2).int32_value(), 22);
    CHECK_EQ(row2.column(3).string_value(), "bb");
  }

  // Read all static rows. Verify rows and that they are read in the number of pages expected.
  {
    CHECK_OK(processor->Run("SELECT DISTINCT s1, s2 FROM t;"));
    std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const YQLRow& row0 = row_block->row(0);
    CHECK_EQ(row0.column(0).int32_value(), 4);
    CHECK_EQ(row0.column(1).string_value(), "d");
    const YQLRow& row1 = row_block->row(1);
    CHECK_EQ(row1.column(0).int32_value(), 3);
    CHECK_EQ(row1.column(1).string_value(), "c");
  }

  // Update static and non-static columns together.
  CHECK_VALID_STMT("UPDATE t set s1 = 5, s2 = 'e', c1 = 33, c2 = 'cc' "
                   "WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b';");
  // Update static columns only.
  CHECK_VALID_STMT("UPDATE t set s1 = 6, s2 = 'f' WHERE h1 = 2 AND h2 = 'b';");

  // Test simple query and result.
  {
    CHECK_VALID_STMT("SELECT s1, s2, c1, c2 FROM t WHERE h1 = 1 AND h2 = 'a';");
    std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const YQLRow& row1 = row_block->row(0);
    CHECK_EQ(row1.column(0).int32_value(), 5);
    CHECK_EQ(row1.column(1).string_value(), "e");
    CHECK_EQ(row1.column(2).int32_value(), 11);
    CHECK_EQ(row1.column(3).string_value(), "aa");
    const YQLRow& row2 = row_block->row(1);
    CHECK_EQ(row2.column(0).int32_value(), 5);
    CHECK_EQ(row2.column(1).string_value(), "e");
    CHECK_EQ(row2.column(2).int32_value(), 33);
    CHECK_EQ(row2.column(3).string_value(), "cc");
  }

  // Read all static rows. Verify rows and that they are read in the number of pages expected.
  {
    CHECK_OK(processor->Run("SELECT DISTINCT s1, s2 FROM t;"));
    std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 2);
    const YQLRow& row0 = row_block->row(0);
    CHECK_EQ(row0.column(0).int32_value(), 6);
    CHECK_EQ(row0.column(1).string_value(), "f");
    const YQLRow& row1 = row_block->row(1);
    CHECK_EQ(row1.column(0).int32_value(), 5);
    CHECK_EQ(row1.column(1).string_value(), "e");
  }
}

} // namespace sql
} // namespace yb
