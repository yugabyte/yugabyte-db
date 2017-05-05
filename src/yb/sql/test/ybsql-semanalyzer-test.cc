// Copyright (c) YugaByte, Inc.

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
  SqlEnv* sql_env = CreateSqlEnv();

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
  EXPECT_EQ(std::string("default_time_to_live"), table_property->lhs()->c_str());
  PTConstVarInt::SharedPtr rhs = std::static_pointer_cast<PTConstVarInt>(table_property->rhs());
  EXPECT_EQ(util::VarInt(1000), util::VarInt(rhs->Eval()->c_str()));
}

TEST_F(YbSqlTestAnalyzer, TestCreateTableAnalyze) {
  CreateSimulatedCluster();
  SqlEnv *sql_env = CreateSqlEnv();

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

TEST_F(YbSqlTestAnalyzer, TestCreateTableWithStaticColumn) {
  CreateSimulatedCluster();
  SqlEnv *sql_env = CreateSqlEnv();

  // Test static column analysis.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT(sql_env, "CREATE TABLE foo (h1 int, r1 int, s1 int static, "
                     "PRIMARY KEY ((h1), r1));", &parse_tree);
  // Invalid: hash column cannot be static.
  ANALYZE_INVALID_STMT(sql_env, "CREATE TABLE foo (h1 int, h2 int static, r1 int, "
                       "PRIMARY KEY ((h1, h2), r1));", &parse_tree);
  ANALYZE_INVALID_STMT(sql_env, "CREATE TABLE foo (h1 int static primary key);", &parse_tree);
  // Invalid: range column cannot be static.
  ANALYZE_INVALID_STMT(sql_env, "CREATE TABLE foo (h1 int, r1 int, r2 int static, "
                       "PRIMARY KEY ((h1), r1, r2));", &parse_tree);
  // Invalid: no static column for table hash key column only.
  ANALYZE_INVALID_STMT(sql_env, "CREATE TABLE foo (h1 int, h2 int, s int static, c int, "
                       "PRIMARY KEY ((h1, h2)));", &parse_tree);
}

TEST_F(YbSqlTestAnalyzer, TestDmlWithStaticColumn) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, h2 int, r1 int, r2 int, s1 int static, c1 int, "
                          "PRIMARY KEY ((h1, h2), r1, r2));"));

  SqlEnv *sql_env = CreateSqlEnv();
  ParseTree::UniPtr parse_tree;

  // Test insert with hash key only.
  ANALYZE_VALID_STMT(sql_env, "INSERT INTO t (h1, h2, s1) VALUES (1, 1, 1);", &parse_tree);

  // Test update with hash key only.
  ANALYZE_VALID_STMT(sql_env, "UPDATE t SET s1 = 1 WHERE h1 = 1 AND h2 = 1;", &parse_tree);

  // TODO: Test delete with hash key only.
  // ANALYZE_VALID_STMT(sql_env, "DELETE c1 FROM t WHERE h1 = 1 and r1 = 1;", &parse_tree);

  // Test select with distinct columns.
  ANALYZE_VALID_STMT(sql_env, "SELECT DISTINCT h1, h2, s1 FROM t;", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "SELECT DISTINCT h1, s1 FROM t;", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "SELECT DISTINCT s1 FROM t;", &parse_tree);

  // Invalid: cannot select distinct with non hash primary-key column.
  ANALYZE_INVALID_STMT(sql_env, "SELECT DISTINCT h1, h2, r1, s1 FROM t;", &parse_tree);

  // Invalid: cannot select distinct with non-static column.
  ANALYZE_INVALID_STMT(sql_env, "SELECT DISTINCT h1, h2, c1 FROM t;", &parse_tree);

  // Invalid: cannot select distinct with non hash primary-key / non-static column.
  ANALYZE_INVALID_STMT(sql_env, "SELECT DISTINCT * FROM t;", &parse_tree);

  // Invalid: cannot insert or update with partial range columns.
  ANALYZE_INVALID_STMT(sql_env, "INSERT INTO t (h1, h2, r1, s1) VALUES (1, 1, 1, 1);", &parse_tree);
  ANALYZE_INVALID_STMT(sql_env, "UPDATE t SET s1 = 1 WHERE h1 = 1 AND h2 = 1 AND r1 = 1;",
                       &parse_tree);
}

TEST_F(YbSqlTestAnalyzer, TestWhereClauseAnalyzer) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, r1 varchar, c1 varchar, "
                          "PRIMARY KEY ((h1), r1));"));

  SqlEnv *sql_env = CreateSqlEnv();

  ParseTree::UniPtr parse_tree;
  // OR and NOT logical operator are not supported yet.
  ANALYZE_INVALID_STMT(sql_env, "SELECT * FROM t WHERE h1 = 1 AND r1 = 2 OR r2 = 3", &parse_tree);
  ANALYZE_INVALID_STMT(sql_env, "SELECT * FROM t WHERE h1 = 1 AND NOT r1 = 2", &parse_tree);

  CHECK_OK(processor->Run("DROP TABLE t;"));
}

TEST_F(YbSqlTestAnalyzer, TestBindVariableAnalyzer) {
  CreateSimulatedCluster();
  YbSqlProcessor *processor = GetSqlProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, r1 varchar, c1 varchar, "
                          "PRIMARY KEY ((h1), r1));"));

  SqlEnv *sql_env = CreateSqlEnv();

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT(sql_env, "SELECT * FROM t WHERE h1 = ? AND r1 = ?;", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "UPDATE t set c1 = :1 WHERE h1 = :2 AND r1 = :3;", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "UPDATE t set c1 = ? WHERE h1 = ? AND r1 = ?;", &parse_tree);
  ANALYZE_VALID_STMT(sql_env, "INSERT INTO t (h1, r1, c1) VALUES (?, ?, ?);", &parse_tree);

  // Bind var cannot be used in a logical boolean context.
  ANALYZE_INVALID_STMT(sql_env, "SELECT * FROM t WHERE NOT ?", &parse_tree);

  // Bind var not supported in an expression (yet).
  ANALYZE_INVALID_STMT(sql_env, "SELECT * FROM t WHERE h1 = (- ?)", &parse_tree);
  ANALYZE_INVALID_STMT(sql_env, "SELECT * FROM t WHERE h1 = (- :1)", &parse_tree);

  CHECK_OK(processor->Run("DROP TABLE t;"));
}

}  // namespace sql
}  // namespace yb
