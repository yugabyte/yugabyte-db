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

#include "yb/util/async_util.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/statement.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/yql/cql/ql/util/errcodes.h"

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using strings::Substitute;

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
    return stmt->ExecuteAsync(processor, StatementParameters(),
                              Bind(&TestQLStatement::ExecuteAsyncDone, Unretained(this), cb));
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

TEST_F(TestQLStatement, TestPKIndices) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create test table.
  LOG(INFO) << "Create test table.";
  EXEC_VALID_STMT("create table test_pk_indices "
                  "(h1 int, h2 text, h3 timestamp, r int, primary key ((h1, h2, h3), r));");

  // Prepare a select statement.
  LOG(INFO) << "Prepare select statement.";
  Statement stmt(processor->CurrentKeyspace(),
                 "select * from test_pk_indices where h3 = ? and h1 = ? and h2 = ?;");
  PreparedResult::UniPtr result;
  CHECK_OK(stmt.Prepare(
      &processor->ql_processor(), nullptr /* mem_tracker */, false /* internal */, &result));

  const std::vector<int64_t>& hash_col_indices = result->hash_col_indices();
  EXPECT_EQ(hash_col_indices.size(), 3);
  EXPECT_EQ(hash_col_indices[0], 1);
  EXPECT_EQ(hash_col_indices[1], 2);
  EXPECT_EQ(hash_col_indices[2], 0);

  LOG(INFO) << "Done.";
}

} // namespace ql
} // namespace yb
