//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql.h"

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

ErrorCode YbSql::Process(SessionContext *session_context, const string& sql_stmt) {
  // Parse the statement and get the generated parse tree.
  ErrorCode errcode = parser_->Parse(sql_stmt);
  if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
    return errcode;
  }
  ParseTree::UniPtr parse_tree = parser_->Done();
  DCHECK(parse_tree.get() != nullptr) << "Parse tree is null";

  // Semantic analysis.
  // Traverse, error-check, and decorate the parse tree nodes with datatypes.
  errcode = analyzer_->Analyze(sql_stmt, move(parse_tree));
  if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
    return errcode;
  }
  parse_tree = analyzer_->Done();
  CHECK(parse_tree.get() != nullptr) << "SEM tree is null";

  // Code generation.
  // Code (expression tree or bytecode) pool. This pool is used to allocate expression tree or byte
  // code to be executed. This pool should last as long as the code remains in our system.
  // MemoryContext *code_mem;

  // Code execution.
  // Temporary memory pool that is used during the execution. This pool is deleted as soon as the
  // execution completes.
  // MemoryContext *exec_mem;
  errcode = executor_->Execute(sql_stmt, move(parse_tree), session_context);
  if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
    return errcode;
  }
  parse_tree = executor_->Done();
  CHECK(parse_tree.get() != nullptr) << "Exec tree is null";

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
