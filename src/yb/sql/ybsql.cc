//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql.h"
#include <gflags/gflags.h>

// Currently, the default is set to 0 because we don't cache the metadata. There's no need to
// refresh and clear the stale metadata.
DEFINE_int32(YB_SQL_EXEC_MAX_METADATA_REFRESH_COUNT, 0,
             "The maximum number of times YbSql engine can refresh metadata cache due to schema "
             "version mismatch error when executing a SQL statement");

namespace yb {
namespace sql {

using std::string;
using std::unique_ptr;

YbSql::YbSql()
    : parser_(new Parser()),
      analyzer_(new Analyzer()),
      executor_(new Executor()) {
}

YbSql::~YbSql() {
}

CHECKED_STATUS YbSql::Process(SqlEnv *sql_env, const string& sql_stmt) {
  // Parse the statement and get the generated parse tree.
  RETURN_NOT_OK(parser_->Parse(sql_stmt));
  ParseTree::UniPtr parse_tree = parser_->Done();
  DCHECK(parse_tree.get() != nullptr) << "Parse tree is null";

  Status s;
  int refresh_count = 0;
  while (refresh_count <= FLAGS_YB_SQL_EXEC_MAX_METADATA_REFRESH_COUNT) {
    // Semantic analysis.
    // Traverse, error-check, and decorate the parse tree nodes with datatypes.
    Status sem_status = analyzer_->Analyze(sql_stmt, move(parse_tree), sql_env, refresh_count);
    ErrorCode sem_errcode = analyzer_->error_code();

    // Release the parse tree for the next step in the process.
    parse_tree = analyzer_->Done();
    CHECK(parse_tree.get() != nullptr) << "SEM tree is null";

    // Check error code.
    if (sem_errcode == ErrorCode::DDL_EXECUTION_RERUN_NOT_ALLOWED) {
      // If rerun failed, we report the previous status to users.
      return s;
    }

    // Increment the refresh count.
    refresh_count++;

    // If failure occurs, it could be because the cached descriptors are stale. In that case, we
    // reload the table descriptor and analyze the statement again.
    s = sem_status;
    if (sem_errcode != ErrorCode::SUCCESS) {
      if (sem_errcode != ErrorCode::TABLE_NOT_FOUND) {
        continue;
      }
      return s;
    }

    // TODO(neil) Code generation. We bypass this step at this time.

    // Code execution.
    s = executor_->Execute(sql_stmt, move(parse_tree), sql_env);
    ErrorCode exec_errcode = executor_->error_code();

    // Release the parse tree as we are done.
    parse_tree = executor_->Done();
    CHECK(parse_tree.get() != nullptr) << "Exec tree is null";

    // If the failure occurs because of stale metadata cache, rerun with latest metadata.
    if (exec_errcode != ErrorCode::WRONG_METADATA_VERSION) {
      return s;
    }
  }

  // Return status.
  return s;
}

CHECKED_STATUS YbSql::TestParser(const string& sql_stmt) {
  // Parse the statement and get the generated parse tree.
  RETURN_NOT_OK(parser_->Parse(sql_stmt));
  ParseTree::UniPtr parse_tree = parser_->Done();
  DCHECK(parse_tree.get() != nullptr) << "Parse tree is null";

  // Return status.
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
