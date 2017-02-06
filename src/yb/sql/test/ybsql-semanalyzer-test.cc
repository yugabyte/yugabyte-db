// Copyright (c) YugaByte, Inc.

#include <memory>
#include "yb/sql/test/ybsql-test-base.h"

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
  SqlEnv* sql_env = CreateNewConnectionContext();

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  string create_stmt = "CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY (c1)) WITH "
      "default_time_to_live = 1000";
  CHECK_OK(TestAnalyzer(sql_env, create_stmt, &parse_tree));

  // Now verify the analysis was done correctly.
  TreeNode *tnode = ((parse_tree.get())->root()).get();
  PTListNode *list_node = static_cast<PTListNode*>(tnode);
  EXPECT_EQ(1, list_node->size());
  TreeNode::SharedPtr inner_node = list_node->element(0);
  EXPECT_EQ(static_cast<int>(TreeNodeOpcode::kPTCreateTable), static_cast<int>(
      inner_node->opcode()));
  PTCreateTable *pt_create_table = static_cast<PTCreateTable*>(inner_node.get());

  // Verify table properties.
  PTTablePropertyListNode::SharedPtr table_properties = pt_create_table->table_properties();
  EXPECT_EQ(1, table_properties->size());
  PTTableProperty::SharedPtr table_property = table_properties->element(0);
  EXPECT_EQ(std::string(PTTableProperty::kDefaultTimeToLive), table_property->lhs()->c_str());
  PTConstInt::SharedPtr rhs = std::static_pointer_cast<PTConstInt>(table_property->rhs());
  EXPECT_EQ(1000, rhs->Eval());
}

TEST_F(YbSqlTestAnalyzer, TestCreateTableAnalyze) {
  CreateSimulatedCluster();
  SqlEnv *sql_env = CreateNewConnectionContext();

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  ANALYZE_INVALID_STMT(sql_env, "CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000 AND default_time_to_live = 2000", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1))", &parse_tree);
  ANALYZE_INVALID_STMT(sql_env, "CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000.1", &parse_tree);
}

}  // namespace sql
}  // namespace yb
