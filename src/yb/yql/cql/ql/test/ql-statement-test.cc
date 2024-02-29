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

#include "yb/gutil/strings/substitute.h"

#include "yb/ash/wait_state.h"

#include "yb/client/client.h"

#include "yb/util/async_util.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/statement.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/yql/cql/ql/util/errcodes.h"

using std::string;

namespace yb {
namespace ql {

class TestQLStatement : public QLTestBase {
 public:
  TestQLStatement() : QLTestBase() {
  }

  void ExecuteAsyncDone(
      Callback<void(const Status&)> cb, const Status& s, const ExecutedResult::SharedPtr& result) {
    cb.Run(s);
  }

  Status ExecuteAsync(Statement *stmt, QLProcessor *processor, Callback<void(const Status&)> cb) {
    ADOPT_WAIT_STATE(ash::WaitStateInfo::CreateIfAshIsEnabled<ash::WaitStateInfo>());
    return stmt->ExecuteAsync(processor, StatementParameters(),
                              Bind(&TestQLStatement::ExecuteAsyncDone, Unretained(this), cb));
  }

  void WaitForIndex(const string& table_name, const string& index_name) {
    client::YBTableName yb_table_name(YQL_DATABASE_CQL, kDefaultKeyspaceName, table_name);
    client::YBTableName yb_index_name(YQL_DATABASE_CQL, kDefaultKeyspaceName, index_name);
    ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
        yb_table_name, yb_index_name, INDEX_PERM_READ_WRITE_AND_DELETE));
  }
};

TEST_F(TestQLStatement, TestExecutePrepareAfterTableDrop) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  LOG(INFO) << "Running execute and reprepare after table drop test.";

  // Create test table.
  EXEC_VALID_STMT("create table t (h1 int primary key, c int);");

  // Prepare a select statement.
  Statement stmt(processor->CurrentKeyspace(), "select * from t where h1 = 1;");
  CHECK_OK(stmt.Prepare(&processor->ql_processor()));

  // Drop table.
  EXEC_VALID_STMT("drop table t;");

  // Try executing the statement. Should return STALE_PREPARED_STATEMENT error.
  Synchronizer sync;
  CHECK_OK(ExecuteAsync(
      &stmt, &processor->ql_processor(), Bind(&Synchronizer::StatusCB, Unretained(&sync))));
  Status s = sync.Wait();
  CHECK(s.IsQLError() && GetErrorCode(s) == ErrorCode::STALE_METADATA)
      << "Expect STALE_METADATA but got " << s.ToString();

  LOG(INFO) << "Done.";
}

TEST_F(TestQLStatement, TestPrepareWithUnknownSystemTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  LOG(INFO) << "Running prepare for an unknown system table test.";

  // Prepare a select statement.
  Statement stmt("system", "select * from system.unknown_table;");
  PreparedResult::UniPtr result;
  CHECK_OK(stmt.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  CHECK_EQ(result->table_name().ToString(), "");

  LOG(INFO) << "Done.";
}

TEST_F(TestQLStatement, TestPrepareWithUnknownSystemTableAndUnknownField) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  LOG(INFO) << "Running prepare for an unknown system table test.";

  // Prepare a select statement.
  Statement stmt("system", "select * from system.unknown_table where unknown_field = ?;");
  PreparedResult::UniPtr result;
  CHECK_OK(stmt.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));
  CHECK_EQ(result->table_name().ToString(), "");

  LOG(INFO) << "Done.";
}

void prepareAndCheck(TestQLProcessor* processor,
                     const string& query,
                     std::initializer_list<int64_t> positions) {
  // Prepare a select statement.
  LOG(INFO) << "Prepare statement: " << query;
  PreparedResult::UniPtr result;
  Statement stmt(processor->CurrentKeyspace(), query);
  ASSERT_OK(stmt.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));

  const std::vector<int64_t>& hash_col_indices = result->hash_col_indices();
  EXPECT_EQ(hash_col_indices.size(), positions.size());
  std::initializer_list<int64_t>::iterator it = positions.begin();
  for (int i = 0; it != positions.end(); ++i, ++it) {
    EXPECT_EQ(hash_col_indices[i], *it);
  }
}

TEST_F(TestQLStatement, TestPKIndices) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create test table.
  LOG(INFO) << "Create test table.";
  EXEC_VALID_STMT("create table test_pk_indices "
                  "(h1 int, h2 text, h3 timestamp, r int, primary key ((h1, h2, h3), r));");

  // The hash columns: h1 (pos=1 in SELECT), h2 (pos=2 in SELECT), h3 (pos=0 in SELECT).
  prepareAndCheck(
      processor, "select * from test_pk_indices where h3 = ? and h1 = ? and h2 = ?;", {1, 2, 0});

  EXEC_VALID_STMT("DROP TABLE test_pk_indices;");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(TestQLStatement, TestBindVarPositions) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  LOG(INFO) << "Create test table and index.";
  EXEC_VALID_STMT(
      "CREATE TABLE tbl (h1 INT, h2 TEXT, r1 INT, r2 TEXT, c1 INT, "
      "PRIMARY KEY ((h1, h2), r1, r2)) WITH transactions = {'enabled' : 'true'};");
  EXEC_VALID_STMT("CREATE INDEX ind1 ON tbl ((h1, r2), h2, r1);");
  EXEC_VALID_STMT("CREATE INDEX ind2 ON tbl ((h2, r1), h1, c1);");
  // Wait for read permissions on the indexes.
  WaitForIndex("tbl", "ind1");
  WaitForIndex("tbl", "ind2");

  // Primary index 'tbl' is the chosen scan method. Hash columns: h1, h2.
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h2 = ? AND h1 = ?;", {1, 0});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h2 = ? AND h1 = ? AND r1 IN (?, ?);", {1, 0});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE r1 IN (?, ?) AND h2 = ? AND h1 = ?;", {3, 2});

  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 = ? AND h2 = ?;", {0, 1});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 = ? AND h2 = ? AND r1 = ?;", {0, 1});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE r1 = ? AND h1 = ? AND h2 = ?;", {1, 2});
  prepareAndCheck(processor,
      "SELECT * FROM tbl WHERE h1 = ? AND h2 = ? AND (r1, r2) IN ((?, ?));", {0, 1});
  prepareAndCheck(processor,
      "SELECT * FROM tbl WHERE (r1, r2) IN ((?, ?)) AND h1 = ? AND h2 = ?;", {2, 3});

  // Test operator IN with a hash column.
  // TOFIX: EMPTY HASH-COL POSITIONS ARE NOT EXPECTED HERE.
  //        https://github.com/yugabyte/yugabyte-db/issues/13710
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 IN (?);", {});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 IN (?) AND h2 = ?;", {});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 = ? AND h2 IN (?);", {});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 IN (?, ?) AND h2 = ?;", {});

  // Index 'ind1' is the chosen scan method. Hash columns: h1, r2.
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 = ? AND h2 = ? AND r2 = ?;", {0, 2});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE r2 = ? AND h2 = ? AND h1 = ?;", {2, 0});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE h1 = ? AND r2 = ? AND h2 = ?;", {0, 1});
  prepareAndCheck(processor, "SELECT * FROM tbl WHERE r2 = ? AND h1 = ? AND h2 = ?;", {1, 0});

  // Index 'ind2' is the chosen scan method. Hash columns: h2, r1.
  prepareAndCheck(processor, "SELECT h2, c1 FROM tbl WHERE c1 = ? AND r1 = ? AND h2 = ?;", {2, 1});
  prepareAndCheck(processor, "SELECT h2, c1 FROM tbl WHERE c1 = ? AND h2 = ? AND r1 = ?;", {1, 2});
  prepareAndCheck(processor, "SELECT h2, c1 FROM tbl WHERE h2 = ? AND c1 = ? AND r1 = ?;", {0, 2});
  prepareAndCheck(processor, "SELECT h2, c1 FROM tbl WHERE r1 = ? AND c1 = ? AND h2 = ?;", {2, 0});

  // Test UPDATE.
  prepareAndCheck(processor,
      "UPDATE tbl SET c1 = 0 WHERE h1 = ? AND h2 = ? AND r2 = ? AND r1 = ?;", {0, 1});
  prepareAndCheck(processor,
      "UPDATE tbl SET c1 = 0 WHERE r2 = ? AND r1 = ? AND h2 = ? AND h1 = ?;", {3, 2});

  // Test DELETE.
  prepareAndCheck(processor,
      "DELETE FROM tbl WHERE h2 = ? AND h1 = ? AND r2 = ? AND r1 = ?;", {1, 0});
  prepareAndCheck(processor,
      "DELETE FROM tbl WHERE r2 = ? AND r1 = ? AND h1 = ? AND h2 = ? IF EXISTS;", {2, 3});

  // Miscellaneous test case with a complex name for the hash column. Note that \"j->>'a'\" is just
  // another independent column, and in no way related to the jsonb column j.
  EXEC_VALID_STMT("CREATE TABLE tbl_json (\"j->>'a'\" INT, \"j->>'b'\" INT, j JSONB, "
                  "PRIMARY KEY(\"j->>'a'\")) WITH transactions = {'enabled' : true};");
  prepareAndCheck(processor, "SELECT * FROM tbl_json WHERE j = ?;", {});
  prepareAndCheck(processor, "SELECT * FROM tbl_json WHERE \"j->>'a'\" = ?;", {0});
  prepareAndCheck(processor,
      "SELECT * FROM tbl_json WHERE \"j->>'b'\" = ? AND \"j->>'a'\" = ?;", {1});
  prepareAndCheck(processor, "UPDATE tbl_json SET \"j->>'b'\" = ? WHERE \"j->>'a'\" = ?;", {1});
  prepareAndCheck(processor, "DELETE FROM tbl_json WHERE \"j->>'a'\" = ?;", {0});

  EXEC_VALID_STMT("CREATE TABLE tbl_json2 (h1 INT, \"j->>'a'\" INT, j JSONB, v1 INT, "
                  "PRIMARY KEY((h1, \"j->>'a'\"))) WITH transactions = {'enabled' : true};");
  EXEC_VALID_STMT("CREATE INDEX ind ON tbl_json2 ((j->>'b', h1));");
  WaitForIndex("tbl_json2", "ind");

  // Using main table PRIMARY KEY: (h1, j->>'a').
  prepareAndCheck(processor,
      "SELECT * FROM tbl_json2 WHERE v1 = ? AND h1 = ? AND \"j->>'a'\" = ?;", {1, 2});
  prepareAndCheck(processor,
      "UPDATE tbl_json2 SET v1 = ? WHERE \"j->>'a'\" = ? AND h1 = ?;", {2, 1});
  prepareAndCheck(processor, "DELETE FROM tbl_json2 WHERE h1 = ? AND \"j->>'a'\" = ?;", {0, 1});
  // Using index PRIMARY KEY: (j->>'b', h1)
  prepareAndCheck(processor,
      "SELECT h1 FROM tbl_json2 WHERE j->>'b' = ? AND h1 = ?;", {0, 1});
  prepareAndCheck(processor,
      "SELECT * FROM tbl_json2 WHERE v1 = ? AND h1 = ? AND j->>'b' = ?;", {2, 1});

  // Clean-up.
  EXEC_VALID_STMT("DROP TABLE tbl;");
  EXEC_VALID_STMT("DROP TABLE tbl_json;");
  EXEC_VALID_STMT("DROP TABLE tbl_json2;");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

} // namespace ql
} // namespace yb
