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

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;

namespace yb {
namespace ql {

TEST_F(QLTestBase, TestQLAlterTableRemoveIndexedColumn) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  EXEC_VALID_STMT("CREATE TABLE t(h int, r int, v int, v1 int, PRIMARY KEY(h, r)) "
                  "WITH transactions = { 'enabled' : true };");
  EXEC_VALID_STMT("ALTER TABLE t DROP v1;");
  EXEC_VALID_STMT("CREATE UNIQUE INDEX t_idx ON t(r, v);");
  // Check hash key column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP h;",
      "Alter key column. Can't alter key column");
  // Check indexed range key column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP r;",
      "Feature Not Yet Implemented. Can't drop column used in an index. "
      "Remove 't_idx' index first and try again");
  // Check indexed column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP v;",
      "Feature Not Yet Implemented. Can't drop column used in an index. "
      "Remove 't_idx' index first and try again");
  EXEC_VALID_STMT("DROP INDEX t_idx;");
  // Check range key column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP r;",
      "Execution Error. cannot remove a key column");
  EXEC_VALID_STMT("ALTER TABLE t DROP v;");
}

TEST_F(QLTestBase, TestQLAlterTableRemoveIndexedColumnExpr) {
  // Init the simulated cluster.
      ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  EXEC_VALID_STMT("CREATE TABLE t(h int, r int, v jsonb, PRIMARY KEY(h, r)) "
                  "WITH transactions = { 'enabled' : true };");
  EXEC_VALID_STMT("CREATE INDEX t_idx ON t(v->>'b') INCLUDE (r);");
  // Check indexed range key column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP r;",
      "Feature Not Yet Implemented. Can't drop column used in an index. "
      "Remove 't_idx' index first and try again");
  // Check indexed column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP v;",
      "Feature Not Yet Implemented. Can't drop column used in an index. "
      "Remove 't_idx' index first and try again");
  EXEC_VALID_STMT("DROP INDEX t_idx;");
  // Check range key column can't be dropped
  EXEC_INVALID_STMT_WITH_ERROR("ALTER TABLE t DROP r;",
      "Execution Error. cannot remove a key column");
  EXEC_VALID_STMT("ALTER TABLE t DROP v;");
}

} // namespace ql
} // namespace yb
