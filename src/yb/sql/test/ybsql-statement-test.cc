//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <thread>
#include <cmath>

#include "yb/sql/test/ybsql-test-base.h"

#include "yb/sql/statement.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using strings::Substitute;

namespace yb {
namespace sql {

class YbSqlStatement : public YbSqlTestBase {
 public:
  YbSqlStatement() : YbSqlTestBase() {
  }

  void ExecuteAsyncDone(
      Callback<void(const Status&)> cb, const Status& s, ExecutedResult::SharedPtr result) {
    cb.Run(s);
  }

  bool ExecuteAsync(Statement *stmt, SqlProcessor *processor, Callback<void(const Status&)> cb) {
    return stmt->ExecuteAsync(processor, StatementParameters(),
                              Bind(&YbSqlStatement::ExecuteAsyncDone, Unretained(this), cb));
  }

};

TEST_F(YbSqlStatement, TestExecutePrepareAfterTableDrop) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  LOG(INFO) << "Running execute and reprepare after table drop test.";

  // Create test table.
  EXEC_VALID_STMT("create table t (h1 int primary key, c int);");

  // Prepare a select statement.
  Statement stmt(processor->CurrentKeyspace(), "select * from t where h1 = 1;");
  CHECK_OK(stmt.Prepare(processor));

  // Drop table.
  EXEC_VALID_STMT("drop table t;");

  // Try executing the statement. Should return STALE_PREPARED_STATEMENT error.
  Synchronizer sync;
  CHECK(ExecuteAsync(&stmt, processor, Bind(&Synchronizer::StatusCB, Unretained(&sync))));
  Status s = sync.Wait();
  CHECK(s.IsSqlError() && GetErrorCode(s) == ErrorCode::STALE_PREPARED_STATEMENT)
      << "Expect STALE_PREPARED_STATEMENT but got " << s.ToString();

  LOG(INFO) << "Done.";
}

} // namespace sql
} // namespace yb
