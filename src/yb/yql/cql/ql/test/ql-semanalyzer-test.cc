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

#include "yb/common/ql_type.h"

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"

#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/varint.h"

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/pt_table_property.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"

namespace yb {
namespace ql {

using std::string;
using strings::Substitute;
using std::dynamic_pointer_cast;
using namespace std::literals;

class QLTestAnalyzer: public QLTestBase {
 public:
  QLTestAnalyzer() : QLTestBase() {
  }

  // TODO(omer): should also input the index_tree, in order to ensure the id found is the id of the
  // index that should be used, but I do not know how to do this yet, as index_tree has the name of
  // the index while select_tree has the id.
  void TestIndexSelection(const string& select_stmt,
                          const bool use_index,
                          const bool covers_fully) {
    ParseTree::UniPtr parse_tree;
    EXPECT_OK(TestAnalyzer(select_stmt, &parse_tree));

    TreeNode::SharedPtr root = parse_tree->root();
    CHECK_EQ(TreeNodeOpcode::kPTSelectStmt, root->opcode());
    auto pt_select_stmt = std::static_pointer_cast<PTSelectStmt>(root);
    auto pt_child_select = pt_select_stmt->child_select();
    EXPECT_EQ(pt_child_select != nullptr, use_index) << select_stmt;
    EXPECT_EQ(pt_child_select != nullptr && pt_child_select->covers_fully(), covers_fully)
        << select_stmt;
  }

  PTJsonColumnWithOperators* RetrieveJsonColumn(const ParseTree::UniPtr& parse_tree,
                                                const DataType& expected_type) {
    auto select_stmt = std::dynamic_pointer_cast<PTSelectStmt>(parse_tree->root());
    auto ptrelation2 = std::dynamic_pointer_cast<PTRelation2>(select_stmt->where_clause());
    EXPECT_EQ(expected_type, ptrelation2->op1()->ql_type()->main());
    return std::dynamic_pointer_cast<PTJsonColumnWithOperators>(ptrelation2->op1()).get();
  }

  std::string RetrieveJsonElement(const PTJsonColumnWithOperators* const jsoncolumn, int index) {
    auto element = dynamic_pointer_cast<PTJsonOperator>(jsoncolumn->operators()->element(index));
    auto text = dynamic_pointer_cast<PTConstText>(element->arg());
    return string(text->value()->c_str());
  }
};

TEST_F(QLTestAnalyzer, TestCreateTablePropertyAnalyzer) {
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
  auto from_str = ASSERT_RESULT(VarInt::CreateFromString(rhs->Eval()->c_str()));
  EXPECT_EQ(VarInt(1000), from_str);
}

TEST_F(QLTestAnalyzer, TestCreateTableAnalyze) {
  CreateSimulatedCluster();

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;

  // Duplicate hash and cluster columns.
  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "((c1, c1)))", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "((c1), c1))", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "((c1), c2, c2))", &parse_tree);

  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000 AND default_time_to_live = 2000", &parse_tree);
  ANALYZE_VALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000", &parse_tree);
  ANALYZE_VALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1))", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE TABLE foo (c1 int, c2 int, c3 int, PRIMARY KEY "
      "(c1)) WITH default_time_to_live = 1000.1", &parse_tree);
}

TEST_F(QLTestAnalyzer, TestCreateTableWithStaticColumn) {
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

TEST_F(QLTestAnalyzer, TestJsonColumn) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, c1 jsonb, c2 int, PRIMARY KEY (h1));"));

  ParseTree::UniPtr parse_tree;

  ANALYZE_VALID_STMT("SELECT * FROM t WHERE c1->'a'->'b'->>'c' = '1'", &parse_tree);
  auto jsoncolumn = RetrieveJsonColumn(parse_tree, DataType::STRING);
  EXPECT_TRUE(jsoncolumn != nullptr);
  EXPECT_TRUE(jsoncolumn->name()->IsSimpleName());
  EXPECT_EQ("c1", jsoncolumn->name()->first_name());

  EXPECT_EQ(3, jsoncolumn->operators()->size());
  EXPECT_EQ(JsonOperator::JSON_OBJECT,
            dynamic_pointer_cast<PTJsonOperator>(
            jsoncolumn->operators()->element(0))->json_operator());
  EXPECT_EQ("a", RetrieveJsonElement(jsoncolumn, 0));

  EXPECT_EQ(JsonOperator::JSON_OBJECT,
            dynamic_pointer_cast<PTJsonOperator>(
            jsoncolumn->operators()->element(1))->json_operator());
  EXPECT_EQ("b", RetrieveJsonElement(jsoncolumn, 1));

  EXPECT_EQ(JsonOperator::JSON_TEXT,
            dynamic_pointer_cast<PTJsonOperator>(
            jsoncolumn->operators()->element(2))->json_operator());
  EXPECT_EQ("c", RetrieveJsonElement(jsoncolumn, 2));

  ANALYZE_VALID_STMT("SELECT * FROM t WHERE c1->>'a' = '1'", &parse_tree);
  auto jsoncolumn1 = RetrieveJsonColumn(parse_tree, DataType::STRING);
  EXPECT_EQ(1, jsoncolumn1->operators()->size());
  EXPECT_EQ(JsonOperator::JSON_TEXT,
            dynamic_pointer_cast<PTJsonOperator>(
            jsoncolumn1->operators()->element(0))->json_operator());
  EXPECT_EQ("a", RetrieveJsonElement(jsoncolumn1, 0));
  ANALYZE_VALID_STMT("SELECT * FROM t WHERE c1->'a' = '1'", &parse_tree);
  auto jsoncolumn2 = RetrieveJsonColumn(parse_tree, DataType::JSONB);
  EXPECT_EQ(1, jsoncolumn2->operators()->size());
  EXPECT_EQ(JsonOperator::JSON_OBJECT,
            dynamic_pointer_cast<PTJsonOperator>(
            jsoncolumn2->operators()->element(0))->json_operator());
  EXPECT_EQ("a", RetrieveJsonElement(jsoncolumn2, 0));

  // Comparing string and integer.
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE c1->'a'->'b'->>'c' = 1", &parse_tree);
  // Column is not json.
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE c2->>'a' = '1'", &parse_tree);
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1->'a' = '1'", &parse_tree);

  // Update json column.
  ANALYZE_VALID_STMT("UPDATE t SET c1->'a'->'b'->'c' = '2' WHERE h1 = 1", &parse_tree);
  ANALYZE_VALID_STMT("UPDATE t SET c1->'a' = '2' WHERE h1 = 1", &parse_tree);

  // Invalid updates.
  ANALYZE_INVALID_STMT("UPDATE t SET c1->>'a' = '2' WHERE h1 = 1", &parse_tree);
  ANALYZE_INVALID_STMT("UPDATE t SET c1.a.b.c = '2' WHERE h1 = 1", &parse_tree);
}

TEST_F(QLTestAnalyzer, TestDmlWithStaticColumn) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
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
  ANALYZE_VALID_STMT("SELECT DISTINCT s1 FROM t WHERE h1 = 1 AND h2 = 1;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT DISTINCT s1 FROM t;", &parse_tree);

  // Invalid: Missing a hash primary-key column.
  ANALYZE_INVALID_STMT("SELECT DISTINCT h1, s1 FROM t;", &parse_tree);
  ANALYZE_INVALID_STMT("SELECT DISTINCT h1, s1 FROM t WHERE h2 = 1;", &parse_tree);

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

TEST_F(QLTestAnalyzer, TestWhereClauseAnalyzer) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, r1 varchar, c1 varchar, "
                          "PRIMARY KEY ((h1), r1));"));

  ParseTree::UniPtr parse_tree;
  // OR and NOT logical operator are not supported yet.
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1 = 1 AND r1 = 2 OR r2 = 3", &parse_tree);
  ANALYZE_INVALID_STMT("SELECT * FROM t WHERE h1 = 1 AND NOT r1 = 2", &parse_tree);

  CHECK_OK(processor->Run("DROP TABLE t;"));
}

TEST_F(QLTestAnalyzer, TestIfClauseAnalyzer) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
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

TEST_F(QLTestAnalyzer, TestBindVariableAnalyzer) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
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

TEST_F(QLTestAnalyzer, TestBindVariableInChildSelect) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  EXEC_VALID_STMT("CREATE TABLE tbl ("
                  "  h1 INT, h2 TEXT, r1 INT, r2 TEXT, c1 INT, PRIMARY KEY ((h1, h2), r1, r2)"
                  ") WITH transactions = {'enabled': 'true'};");
  EXEC_VALID_STMT("CREATE INDEX ind ON tbl ((h1, r2), h2, r1);");

  // Wait for read permissions on the index.
  client::YBTableName table_name(YQL_DATABASE_CQL, kDefaultKeyspaceName, "tbl");
  client::YBTableName index_name(YQL_DATABASE_CQL, kDefaultKeyspaceName, "ind");
  ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_name, INDEX_PERM_READ_WRITE_AND_DELETE));

  const client::YBTableInfo table_info = ASSERT_RESULT(client_->GetYBTableInfo(table_name));
  // The first column id is 0 in Release and 10 in Debug.
  const int base_id = table_info.schema.ColumnId(0);

  auto checkPTBindVar =
      [base_id](PTSelectStmt::PTBindVarSet::const_iterator it,
                const char* name, int id, int64_t pos) {
        ASSERT_STREQ((*it)->name()->c_str(), name);
        ASSERT_EQ((*it)->pos(), pos);
        ASSERT_NE((*it)->hash_col(), nullptr);
        ASSERT_EQ((*it)->hash_col()->id(), base_id + id);
      };

  EXEC_INVALID_STMT_WITH_ERROR(
      "SELECT h2, r1, c1 FROM tbl WHERE h1 = :h1 AND h2 = :h2 AND r2 = :r2;",
      "no bind variable available");
  // The block here is needed to clean temporary shared pointers (like 'main_select')
  // before the ParserTree destructor call.
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTSelectStmt, root->opcode());
    PTSelectStmt::SharedPtr main_select = std::static_pointer_cast<PTSelectStmt>(root);
    // PTSelectStmt::table_name() returns a referenced table name from the 'FROM' clause.
    ASSERT_EQ(main_select->table_name(), table_name);
    ASSERT_EQ(main_select->table()->name(), table_name);
    ASSERT_FALSE(main_select->table()->IsIndex());

    // In the SELECT statement the bind variables positions: :h1 - 0, :h2 - 1, :r2 - 2.
    {
      const PTSelectStmt::PTBindVarSet& hash_col_bindvars = main_select->TEST_hash_col_bindvars();
      ASSERT_EQ(2, hash_col_bindvars.size());
      PTSelectStmt::PTBindVarSet::const_iterator it = hash_col_bindvars.begin();
      // In the table hash columns: h1, h2 - positions in SELECT: 0, 1.
      checkPTBindVar(it, "h1", 0, 0);
      checkPTBindVar(++it, "h2", 1, 1);
    }

    PTSelectStmt::SharedPtr child_select = main_select->child_select();
    ASSERT_NE(child_select, nullptr);
    ASSERT_EQ(child_select->table()->name(), index_name);
    ASSERT_TRUE(child_select->table()->IsIndex());
    ASSERT_FALSE(child_select->covers_fully());
    {
      const PTSelectStmt::PTBindVarSet& hash_col_bindvars = child_select->TEST_hash_col_bindvars();
      ASSERT_EQ(2, hash_col_bindvars.size());
      PTSelectStmt::PTBindVarSet::const_iterator it = hash_col_bindvars.begin();
      // In the index hash columns: h1, r2 - positions in SELECT: 0, 2.
      checkPTBindVar(it, "h1", 0, 0);
      checkPTBindVar(++it, "r2", 1, 2);
    }
  }

  EXEC_VALID_STMT("DROP TABLE tbl;");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(QLTestAnalyzer, TestCreateIndex) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, h2 text, r1 int, r2 text, c1 int, c2 text, "
                          "PRIMARY KEY ((h1, h2), r1, r2)) "
                          "with transactions = {'enabled':true};"));

  // Analyze the sql statement.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT("CREATE INDEX i ON t ((r1), r2);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t ((r1, r2), h1, h2);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t ((h1, h2), r1, r2);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (h1);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (r1);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (c1);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (r2, r1) INCLUDE (c1);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (r2, r1, h1, h2) INCLUDE (c1) WITH CLUSTERING ORDER BY "
                     "(r1 DESC, h1 DESC, h2 ASC);", &parse_tree);

  // Duplicate covering columns - should succeed
  ANALYZE_VALID_STMT("CREATE INDEX i ON t ((r1), r2) INCLUDE (r1);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t ((r1), r2) INCLUDE (r2);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (r1, r2, c1) INCLUDE (c1);", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t (r1, r2) INCLUDE (c1, c1);", &parse_tree);

  // Duplicate primary key columns.
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t (r1, r1);", &parse_tree);

  // Non-clustering key column in order by.
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t (r2, r1) INCLUDE (c1) WITH CLUSTERING ORDER BY "
                       "(r2 DESC, r1 ASC);", &parse_tree);

  // Non-existent table.
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t2 (r1, r2);", &parse_tree);

  // Index on system table cannot be created.
  ANALYZE_INVALID_STMT("CREATE INDEX i ON system_schema.tables (id);", &parse_tree);


  CHECK_OK(processor->Run("CREATE TABLE t2 (h1 int, h2 text, r1 int, r2 text, c list<int>, "
                          "PRIMARY KEY ((h1, h2), r1, r2)) "
                          "with transactions = {'enabled':true};"));
  // Unsupported complex type.
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t2 (c);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t2 ((r1), c);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t2 (r1, r2) INCLUDE (c);", &parse_tree);

  CHECK_OK(processor->Run("CREATE TABLE t3 (k int primary key, c int);"));

  // Index on non-transactional table.
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t3 (c);", &parse_tree);

  // JSON secondary index on attributes.
  CHECK_OK(processor->Run("CREATE TABLE t4 (h1 int, h2 text, r1 int, r2 text, j jsonb, "
                          "PRIMARY KEY ((h1, h2), r1, r2)) "
                          "with transactions = {'enabled':true};"));

  // Analyze the sql statement.
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->>'a');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->'a'->>'b');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->'a'->'b'->>'c');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->>'a'));", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->'a'->>'b'));", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->'a'->'b'->>'c'));", &parse_tree);
  // Multiple JSON columns, as hash and range columns.
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->>'a', j->>'x');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->>'a'), j->>'x');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->>'a', j->>'x'));", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->'a'->>'b', j->'x'->>'y');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->'a'->>'b'), j->'x'->>'y');", &parse_tree);
  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 ((j->'a'->>'b', j->'x'->>'y'));", &parse_tree);
  // Covering column.
  // TODO: sem_context.h:215: Check failed: sem_state_ State variable is not set for the expression
//  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->>'a') INCLUDE (j->>'x');", &parse_tree);
//  ANALYZE_VALID_STMT("CREATE INDEX i ON t4 (j->>'a') INCLUDE (j->>'x', j->>'k');", &parse_tree);
}

TEST_F(QLTestAnalyzer, TestCreateLocalIndex) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h1 int, h2 text, r1 int, r2 text, c1 int, c2 text, "
                          "PRIMARY KEY ((h1, h2), r1, r2));"));

  // Analyze the sql statement.
  // Because transactions are not enabled, an index creation will fail iff the index is not local.
  // TODO: Mark indexes on ((h1, h2)...) as valid once local index is implemented.
  ParseTree::UniPtr parse_tree;
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2));", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2), c2, c1);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2), r2, c1);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2), r1, r2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2), r1, r2, c1, c2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t (h1, h2, r1, r2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1), h2, r1, r2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((r1, r2), h1, h2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h2), r1, r2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2, r1));", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, h2, r1, r2), c1, c2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t ((h1, r1), h2);", &parse_tree);
  ANALYZE_INVALID_STMT("CREATE INDEX i ON t (h1, h2);", &parse_tree);
}


TEST_F(QLTestAnalyzer, TestIndexSelection) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  EXPECT_OK(processor->Run("CREATE TABLE t (h1 int, h2 int, r1 int, r2 int, c1 int, c2 int, "
                           "PRIMARY KEY ((h1, h2), r1, r2)) "
                           "with transactions = {'enabled':true};"));

  ParseTree::UniPtr select_parse_tree, parse_tree;

  // Should not use index if there is no index.
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND h2 = 1 AND r1 = 1", false, false);

  EXPECT_OK(processor->Run("CREATE INDEX i1 ON t ((h1, h2), r1);"));
  EXPECT_OK(processor->Run("CREATE INDEX i2 ON t ((h1, h2), c2, c1);"));
  EXPECT_OK(processor->Run("CREATE INDEX i3 ON t ((h1, h2), r2) INCLUDE (c1);"));
  EXPECT_OK(processor->Run("CREATE INDEX i4 ON t ((h1, h2), r2) INCLUDE (c2);"));
  EXPECT_OK(processor->Run("CREATE INDEX i5 ON t ((h1, h2), c1);"));
  EXPECT_OK(processor->Run("CREATE INDEX i6 ON t ((c1, r2), c2);"));
  EXPECT_OK(processor->Run("CREATE INDEX i7 ON t ((h2, h1), c1) INCLUDE (c2);"));

  client::YBTableName table_name(YQL_DATABASE_CQL, kDefaultKeyspaceName, "t");
  // Wait for read permissions on all indexes.
  for (int i = 1; i <= 7; ++i) {
    client::YBTableName index_name(YQL_DATABASE_CQL, kDefaultKeyspaceName, Format("i$0", i));
    EXPECT_OK(client_->WaitUntilIndexPermissionsAtLeast(table_name,
                                                        index_name,
                                                        INDEX_PERM_READ_WRITE_AND_DELETE));
  }
  processor->RemoveCachedTableDesc(table_name);

  // Should select from the indexed table.
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND c2 = 1 AND c1 = 1", false, false);
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND h2 = 1 AND r1 = 1", false, false);
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND c1 = 1 AND c2 = 1", false, false);

  // None for i1, as the table itself is always better.

  // Should use i2.
  TestIndexSelection("SELECT c2, r1 FROM t WHERE h1 = 1 AND h2 = 1 AND c2 = 1 AND c1 = 1",
                     true, true);
  // Check that fully specified beats range clauses.
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND h2 = 1 AND c2 = 1 AND r1 > 0 AND r1 < 2",
                     true, true);

  // Should use i3.
  TestIndexSelection("SELECT c1 FROM t WHERE h1 = 1 AND h2 = 1 AND r2 = 1", true, true);

  // Should use i3 as uncovered index.
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND h2 = 1 AND r2 = 1", true, false);

  // Should use i4.
  TestIndexSelection("SELECT c2, r1 FROM t WHERE h1 = 1 AND h2 = 1 AND r2 = 1", true, true);
  TestIndexSelection("SELECT h2, c2, r1 FROM t WHERE h1 = 1 AND h2 = 1 AND r2 = 1 AND "
                     "c2 = 1 AND r1 > 0 AND r1 < 2", true, true);

  // Should use i5.
  // Check that local beats-non-local.
  TestIndexSelection("SELECT c1 FROM t WHERE h1 = 1 AND h2 = 1 AND c1 = 1", true, true);

  // Should use i6.
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND h2 = 1 AND r2 = 1 AND c1 = 1 AND c2 = 1",
                     true, true);

  // Should use i7.
  // Check that non-local beats local if it can perform the read.
  TestIndexSelection("SELECT * FROM t WHERE h1 = 1 AND h2 = 1 AND c1 = 1", true, true);
  TestIndexSelection("SELECT c2 FROM t WHERE h1 = 1 AND h2 = 1 AND r2 = 1 AND c1 = 1", true, true);
}

TEST_F(QLTestAnalyzer, TestIndexBasedOnJsonAttribute) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  EXPECT_OK(processor->Run("CREATE TABLE t (h1 int, h2 int, r1 int, r2 int, j jsonb, "
                           "PRIMARY KEY ((h1, h2), r1, r2)) "
                           "with transactions = {'enabled':true};"));

  EXPECT_OK(processor->Run("CREATE INDEX i1 ON t (j->>'a');"));
  EXPECT_NOK(processor->Run("CREATE INDEX i2 ON t (j->3);"));
}

TEST_F(QLTestAnalyzer, TestTruncate) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h int PRIMARY KEY);"));

  // Analyze the TRUNCATE statement.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT("TRUNCATE TABLE t;", &parse_tree);
  ANALYZE_VALID_STMT(Substitute("TRUNCATE TABLE $0.t;", kDefaultKeyspaceName), &parse_tree);

  // No such keyspace
  ANALYZE_INVALID_STMT("TRUNCATE TABLE invalid_keyspace.t;", &parse_tree);
  // No such table
  ANALYZE_INVALID_STMT("TRUNCATE TABLE invalid_table;", &parse_tree);
  // Only one table can be truncated in each statement.
  ANALYZE_INVALID_STMT("TRUNCATE TABLE t1, t2;", &parse_tree);
  // Invalid qualified table name.
  ANALYZE_INVALID_STMT("TRUNCATE TABLE k.t.c;", &parse_tree);
}

TEST_F(QLTestAnalyzer, TestMisc) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();

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

TEST_F(QLTestAnalyzer, TestOffsetLimitClause) {
  CreateSimulatedCluster();
  TestQLProcessor *processor = GetQLProcessor();
  CHECK_OK(processor->Run("CREATE TABLE t (h int PRIMARY KEY);"));

  // Analyze the LIMIT/OFFSET clause.
  ParseTree::UniPtr parse_tree;
  ANALYZE_VALID_STMT("SELECT * FROM t LIMIT 10 OFFSET 10;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT * FROM t OFFSET 10;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT * FROM t LIMIT 10;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT * FROM t OFFSET 0;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT * FROM t LIMIT 0;", &parse_tree);
  ANALYZE_VALID_STMT("SELECT * FROM t OFFSET 10 LIMIT 10;", &parse_tree);
}

}  // namespace ql
}  // namespace yb
