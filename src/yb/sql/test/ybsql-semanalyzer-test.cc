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

#include <memory>
#include "yb/sql/test/ybsql-test-base.h"
#include "yb/util/varint.h"

namespace yb {
namespace sql {

using std::make_shared;
using std::string;

class YbSqlTestAnalyzer: public YbSqlTestBase {
 public:
  YbSqlTestAnalyzer() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlTestAnalyzer, TestCreateTablePropertyAnalyzer) {
  CreateSimulatedCluster();

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  string create_stmt = "CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY (c1)) WITH "
      "default_time_to_live = 1000";
  CHECK_OK(TestAnalyzer(create_stmt, &parse_tree));

  // Now verify the analysis was done correctly.
  TreeNode::SharedPtr root = parse_tree->root();
  EXPECT_EQ(TreeNodeOpcode::kPTCreateTable, root->opcode());
  PTCreateTable::SharedPtr pt_create_table = std::static_pointer_cast<PTCreateTable>(root);

  // Verify table properties.
  PTTablePropertyListNode::SharedPtr table_properties = pt_create_table->table_properties();
  EXPECT_EQ(1, table_properties->size());
  PTTableProperty::SharedPtr table_property = table_properties->element(0);
  EXPECT_EQ(std::string("default_time_to_live"), table_property->lhs()->c_str());
  PTConstVarInt::SharedPtr rhs = std::static_pointer_cast<PTConstVarInt>(table_property->rhs());
  EXPECT_EQ(util::VarInt(1000), util::VarInt(rhs->Eval()->c_str()));
}

TEST_F(YbSqlTestAnalyzer, TestCreateTableAnalyze) {
  CreateSimulatedCluster();

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000 AND default_time_to_live = 2000", &parse_tree);
  ANALYZE_VALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000", &parse_tree);
  ANALYZE_VALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1))", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000.1", &parse_tree);
}

TEST_F(YbSqlTestAnalyzer, TestCreateTableWithStaticColumn) {
  CreateSimulatedCluster();

  // Test static column analysis.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT("CREATE TABLE foo (h1 int, r1 int, s1 int static, "
                     "PRIMARY KEY ((h1), r1));", &parse_tree);
  // Invalid: hash column cannot be static.
  ANALYZE_INVALID_STMT("CREATE TABLE foo (h1 int, h2 int static, r1 int, "
                       "PRIMARY KEY ((h1, h2), r1));", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE TABLE foo (h1 int static primary key);", &parse_tree);
  // Invalid: range column cannot be static.
  ANALYZE_INVALID_STMT("CREATE TABLE foo (h1 int, r1 int, r2 int static, "
                       "PRIMARY KEY ((h1), r1, r2));", &parse_tree);
  // Invalid: no static column for table hash key column only.
  ANALYZE_INVALID_STMT("CREATE TABLE foo (h1 int, h2 int, s int static, c int, "
                       "PRIMARY KEY ((h1, h2)));", &parse_tree);
}

TEST_F(YbSqlTestAnalyzer, TestDmlWithStaticColumn) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, h2 int, r1 int, r2 int, s1 int static, c1 int, "
                          "PRIMARY KEY ((h1, h2), r1, r2));"));

  ParseTree::UniPtr parse_tree;

  // Test insert with hash key only.
  ANALYZE_VALID_STMT("INSERT INTO t (h1, h2, s1) VALUES (1, 1, 1);", &parse_tree);

  // Test update with hash key only.
  ANALYZE_VALID_STMT("UPDATE t SET s1 = 1 WHERE h1 = 1 AND h2 = 1;", &parse_tree);

  // TODO: Test delete with hash key only.
  // ANALYZE_VALID_STMT("DELETE c1 FROM t WHERE h1 = 1 and r1 = 1;", &parse_tree);

  // Test select with distinct columns.
  ANALYZE_VALID_STMT("SELECT DISTINCT h1, h2, s1 FROM t;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT DISTINCT h1, s1 FROM t;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT DISTINCT s1 FROM t;", &parse_tree);

  // Invalid: cannot select distinct with non hash primary-key column.
  ANALYZE_INVALID_STMT("SELECT DISTINCT h1, h2, r1, s1 FROM t;", &parse_tree);

  // Invalid: cannot select distinct with non-static column.
  ANALYZE_INVALID_STMT("SELECT DISTINCT h1, h2, c1 FROM t;", &parse_tree);

  // Invalid: cannot select distinct with non hash primary-key / non-static column.
  ANALYZE_INVALID_STMT("SELECT DISTINCT * FROM t;", &parse_tree);

  // Invalid: cannot insert or update with partial range columns.
  ANALYZE_INVALID_STMT("INSERT INTO t (h1, h2, r1, s1) VALUES (1, 1, 1, 1);", &parse_tree);
  ANALYZE_INVALID_STMT("UPDATE t SET s1 = 1 WHERE h1 = 1 AND h2 = 1 AND r1 = 1;", &parse_tree);
}

TEST_F(YbSqlTestAnalyzer, TestWhereClauseAnalyzer) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, r1 varchar, c1 varchar, "
                          "PRIMARY KEY ((h1), r1));"));

  ParseTree::UniPtr parse_tree;
  // OR and NOT logical operator are not supported yet.
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1 = 1 AND r1 = 2 OR r2 = 3", &parse_tree);
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1 = 1 AND NOT r1 = 2", &parse_tree);

  CHECK_OK(processor->Run("DROP TABLE t;"));
}

TEST_F(YbSqlTestAnalyzer, TestIfClauseAnalyzer) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, r1 int, c1 int, "
                          "PRIMARY KEY ((h1), r1));"));

  ParseTree::UniPtr parse_tree;
  // Valid case: if not exists or if <col> = xxx.
  ANALYZE_VALID_STMT("UPDATE t SET c1 = 1 WHERE h1 = 1 AND r1 = 1 IF NOT EXISTS or c1 = 0",
                     &parse_tree);

  // Invalid cases: primary key columns not allowed in if clause.
  ANALYZE_INVALID_STMT("UPDATE t SET c1 = 1 WHERE h1 = 1 AND r1 = 1 IF h1 = 1", &parse_tree);
  ANALYZE_INVALID_STMT("UPDATE t SET c1 = 1 WHERE h1 = 1 AND r1 = 1 IF r1 = 1", &parse_tree);
  CHECK_OK(processor->Run("DROP TABLE t;"));
}

TEST_F(YbSqlTestAnalyzer, TestBindVariableAnalyzer) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, r1 varchar, c1 varchar, "
                          "PRIMARY KEY ((h1), r1));"));

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT("SELECT * FROM t WHERE h1 = ? AND r1 = ?;", &parse_tree);
  ANALYZE_VALID_STMT("UPDATE t set c1 = :1 WHERE h1 = :2 AND r1 = :3;", &parse_tree);
  ANALYZE_VALID_STMT("UPDATE t set c1 = ? WHERE h1 = ? AND r1 = ?;", &parse_tree);
  ANALYZE_VALID_STMT("INSERT INTO t (h1, r1, c1) VALUES (?, ?, ?);", &parse_tree);

  // Bind var cannot be used in a logical boolean context.
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE NOT ?", &parse_tree);

  // Bind var not supported in an expression (yet).
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1 = (- ?)", &parse_tree);
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1 = (- :1)", &parse_tree);

  CHECK_OK(processor->Run("DROP TABLE t;"));
}

TEST_F(YbSqlTestAnalyzer, TestMisc) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();

  // Analyze misc empty sql statements.
  ParseTree::UniPtr parse_tree;
  CHECK_OK(TestAnalyzer("", &parse_tree));
  EXPECT_TRUE(parse_tree->root() == nullptr);
  CHECK_OK(TestAnalyzer(";", &parse_tree));
  EXPECT_TRUE(parse_tree->root() == nullptr);
  CHECK_OK(TestAnalyzer(" ;  ;  ", &parse_tree));
  EXPECT_TRUE(parse_tree->root() == nullptr);

  // Invalid: multi-statement not supported.
  CHECK_OK(processor->Run("CREATE TABLE t (h INT PRIMARY KEY, c INT);"));
  ANALYZE_INVALID_STMT("SELECT * FROM t; SELECT C FROM t;", &parse_tree);
}

}  // namespace sql
}  // namespace yb
