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

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"


namespace yb {
namespace master {
class CatalogManager;
class Master;
}
namespace ql {

#define EXEC_DUPLICATE_OBJECT_CREATE_STMT(stmt)                                \
  do {                                                                  \
    Status s = processor->Run(stmt);                                    \
    EXPECT_FALSE(s.ok());                                               \
    EXPECT_FALSE(s.ToString().find("Duplicate Object. Object") == string::npos); \
  } while (false)

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

  // Create the table and index.
  EXEC_VALID_STMT(CreateTableStmt(table1));
  EXEC_VALID_STMT(CreateIndexStmt(index1));

  // Verify that CREATE statements fail for table and index that have already been created.
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateTableStmt(table1));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateIndexStmt(index1));

  // Verify that all 'CREATE ... IF EXISTS' statements succeed for objects that have already been
  // created.
  EXEC_VALID_STMT(CreateTableIfNotExistsStmt(table1));
  EXEC_VALID_STMT(CreateIndexIfNotExistsStmt(index1));

  const string drop_stmt = "DROP TABLE human_resource1;";
  EXEC_VALID_STMT(drop_stmt);
}

} // namespace ql
} // namespace yb
