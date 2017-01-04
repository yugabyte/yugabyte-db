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

ErrorCode YbSql::Process(SqlEnv *sql_env, const string& sql_stmt) {
  // Parse the statement and get the generated parse tree.
  ErrorCode errcode = parser_->Parse(sql_stmt);
  if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
    return errcode;
  }
  ParseTree::UniPtr parse_tree = parser_->Done();
  DCHECK(parse_tree.get() != nullptr) << "Parse tree is null";

  int refresh_count = 0;
  while (refresh_count <= FLAGS_YB_SQL_EXEC_MAX_METADATA_REFRESH_COUNT) {
    // Semantic analysis.
    // Traverse, error-check, and decorate the parse tree nodes with datatypes.
    ErrorCode sem_errcode =
      analyzer_->Analyze(sql_stmt, move(parse_tree), sql_env, refresh_count);
    parse_tree = analyzer_->Done();
    CHECK(parse_tree.get() != nullptr) << "SEM tree is null";
    if (sem_errcode == ErrorCode::DDL_EXECUTION_RERUN_NOT_ALLOWED) {
      // If rerun failed, we report the previous errorcode to users.
      break;
    }
    errcode = sem_errcode;

    // If failure occurs, it could be because the cached descriptors are stale. In that case, we
    // reload the table descriptor and analyze the statement again.
    if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
      if (errcode != ErrorCode::FDW_TABLE_NOT_FOUND) {
        refresh_count++;
        continue;
      }
      break;
    }

    // TODO(neil) Code generation. We bypass this step at this time.

    // Code execution.
    errcode = executor_->Execute(sql_stmt, move(parse_tree), sql_env);
    parse_tree = executor_->Done();
    CHECK(parse_tree.get() != nullptr) << "Exec tree is null";

    // If the failure occurs because of stale metadata cache, rerun with latest metadata.
    if (errcode != ErrorCode::WRONG_METADATA_VERSION) {
      break;
    }
    refresh_count++;
  }

  // Return status.
  return errcode;
}

ErrorCode YbSql::TestParser(const string& sql_stmt) {
  // Parse the statement and get the generated parse tree.
  ErrorCode errcode = parser_->Parse(sql_stmt);
  if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
    return errcode;
  }
  ParseTree::UniPtr parse_tree = parser_->Done();
  DCHECK(parse_tree.get() != nullptr) << "Parse tree is null";

  // Return status.
  return errcode;
}

}  // namespace sql
}  // namespace yb
