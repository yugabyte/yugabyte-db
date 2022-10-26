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

#include "yb/yql/cql/ql/ptree/pt_create_index.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;

namespace yb {
namespace master {
class CatalogManager;
class Master;
}
namespace ql {

#define EXEC_DUPLICATE_OBJECT_CREATE_STMT(stmt)                                \
  EXEC_INVALID_STMT_WITH_ERROR(stmt, "Duplicate Object. Object")

class TestQLCreateIndex : public QLTestBase {
 public:
  TestQLCreateIndex() : QLTestBase() {
  }

  inline const string CreateTableStmt(string params) {
    return "CREATE TABLE " + params;
  }

  inline const string CreateTableIfNotExistsStmt(string params) {
    return "CREATE TABLE IF NOT EXISTS " + params;
  }

  inline const string CreateIndexStmt(string params) {
    return "CREATE INDEX " + params;
  }

  inline const string CreateIndexIfNotExistsStmt(string params) {
    return "CREATE INDEX IF NOT EXISTS " + params;
  }
};

TEST_F(TestQLCreateIndex, TestQLCreateIndexSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string table1 = "human_resource1(id int primary key, name varchar) "
                        "with transactions = {'enabled':true};";
  const string index1 = "i ON human_resource1(name);";
  const string index_no_name = "ON human_resource1(name);";

  // Create the table and index.
  EXEC_VALID_STMT(CreateTableStmt(table1));
  EXEC_VALID_STMT(CreateIndexStmt(index1));
  EXEC_VALID_STMT(CreateIndexStmt(index_no_name));

  // Verify that CREATE statements fail for table and index that have already been created.
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateTableStmt(table1));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateIndexStmt(index1));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateIndexStmt(index_no_name));

  // Verify that all 'CREATE ... IF EXISTS' statements succeed for objects that have already been
  // created.
  EXEC_VALID_STMT(CreateTableIfNotExistsStmt(table1));
  EXEC_VALID_STMT(CreateIndexIfNotExistsStmt(index1));
  EXEC_VALID_STMT(CreateIndexIfNotExistsStmt(index_no_name));

  EXEC_VALID_STMT("DROP TABLE human_resource1;");
}

TEST_F(TestQLCreateIndex, TestQLCreateIndexDefaultName) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  EXEC_VALID_STMT(CreateTableStmt(
      "human_resource(id int primary key, name varchar, surname text, kids int) "
      "with transactions = {'enabled':true};"));

  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource(name);"));
  // Get Index name from the Parse Tree and check it.
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource_name_idx");
  }

  // Test index over 2-3 columns.
  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource(name, surname);"));
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource_name_surname_idx");
  }

  // Check that the covering columns are not included into the index name.
  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource(surname, name) INCLUDE (kids);"));
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource_surname_name_idx");
  }

  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource((surname, name), id);"));
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource_surname_name_id_idx");
  }

  // Duplicate column name.
  EXEC_INVALID_STMT(CreateIndexStmt("ON human_resource(name, name);"));

  EXEC_VALID_STMT("DROP TABLE human_resource;");
}

TEST_F(TestQLCreateIndex, TestQLSpecialSymbolsInIndexDefaultName) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  EXEC_VALID_STMT(CreateTableStmt(
      "human_resource (id int primary key, \"Capital\" int, \"Capital With Spaces\" int, "
      "\"!@#$%^&*-=+()[]{}<>/\\\\,.;:\"\"'\" int) "
      "with transactions = {'enabled':true};"));

  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource(\"Capital\");"));
  // Test Capital symbol.
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource_Capital_idx");
  }

  // Test spaces.
  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource(\"Capital With Spaces\");"));
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource_CapitalWithSpaces_idx");
  }

  // Test different unprintable symbols.
  EXEC_VALID_STMT(CreateIndexStmt("ON human_resource(\"!@#$%^&*-=+()[]{}<>/\\\\,.;:\"\"'\");"));
  {
    TreeNodePtr root = processor->GetLastParseTreeRoot();
    ASSERT_EQ(TreeNodeOpcode::kPTCreateIndex, root->opcode());
    PTCreateIndex::SharedPtr node = std::static_pointer_cast<PTCreateIndex>(root);
    ASSERT_STREQ(node->name()->c_str(), "human_resource__idx");
  }

  EXEC_VALID_STMT("DROP TABLE human_resource;");
}

TEST_F(TestQLCreateIndex, TestQLCreateIndexExpr) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table.
  const string table = "tabj(\"j->'a'->>'b'\" int, \"j->'a'->>'c'\" int, j jsonb,"
                       "     primary key (\"j->'a'->>'b'\"))"
                       "  with transactions = {'enabled':true};";
  EXEC_VALID_STMT(CreateTableStmt(table));

  // Create valid indexes - no name conflict because no column has the same name as jsonb index.
  EXEC_VALID_STMT(CreateIndexStmt("jdx1 ON tabj(j->'a'->>'d');"));
  EXEC_VALID_STMT(CreateIndexStmt("ON tabj(j->'a'->>'d');"));

  EXEC_VALID_STMT(CreateIndexStmt("jdx2 ON tabj(j->'a'->>'d', \"j->'a'->>'c'\");"));
  EXEC_VALID_STMT(CreateIndexStmt("ON tabj(j->'a'->>'d', \"j->'a'->>'c'\");"));

  // Create valid indexes - no name conflict because "j->'a'->>'c'" is not included.
  EXEC_VALID_STMT(CreateIndexStmt("jdx3 ON tabj(j->'a'->>'c');"));
  EXEC_VALID_STMT(CreateIndexStmt("ON tabj(j->'a'->>'c');"));

  // Create invalid index due to duplicate column name "j->'a'->>'b'".
  EXEC_INVALID_STMT(CreateIndexStmt("jdx4 ON tabj(j->'a'->>'b');"));
  EXEC_INVALID_STMT(CreateIndexStmt("ON tabj(j->'a'->>'b');"));

  // Create invalid index due to duplicate column name "j->'a'->>'c'".
  EXEC_INVALID_STMT(CreateIndexStmt("jdx5 ON tabj(j->'a'->>'c') include(\"j->'a'->>'c'\");"));
  EXEC_INVALID_STMT(CreateIndexStmt("ON tabj(j->'a'->>'c') include(\"j->'a'->>'c'\");"));

  EXEC_INVALID_STMT(CreateIndexStmt("jdx6 ON tabj(j->'a'->>'c', \"j->'a'->>'c'\");"));
  EXEC_INVALID_STMT(CreateIndexStmt("jdx7 ON tabj(\"j->'a'->>'c'\", j->'a'->>'c');"));

  // Testing name escaping.
  EXEC_VALID_STMT(CreateTableStmt("tab_escape"
                                  "  (\"C$_col_C$_\" INT PRIMARY KEY,"
                                  "   \"C$_col->>'$J_attr'\" JSONB)"
                                  "  with transactions = {'enabled':true};"));
  EXEC_VALID_STMT(CreateIndexStmt("jdx8 ON tab_escape"
                                  "  (\"C$_col->>'$J_attr'\"->>'\"J$_attr->>C$_col\"');"));
} // v1

} // namespace ql
} // namespace yb
