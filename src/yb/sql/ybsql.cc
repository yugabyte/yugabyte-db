//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <iostream>

#include "yb/sql/ybsql.h"
#include "yb/sql/parser/parse_context.h"

namespace yb {
namespace sql {

using std::string;
using std::unique_ptr;

YbSql::YbSql()
    : parser_(unique_ptr<Parser>(new Parser())) {
}

YbSql::~YbSql() {
}

ErrorCode YbSql::Process(const string& sql_stmt) {
  ErrorCode errcode = parser_->Parse(sql_stmt);
  if (errcode != ErrorCode::SUCCESSFUL_COMPLETION) {
    return errcode;
  }

  // Get the generated parse tree and clear the context.
  ParseTree::UniPtr parse_tree = parser_->Done();

  // Semantic analysis.
  // Traverse, error-check, and decorate the parse tree nodes with datatypes.

  // Code generation.
  // Code (expression tree or bytecode) pool. This pool is used to allocate expression tree or byte
  // code to be executed. This pool should last as long as the code remains in our system.
  // MemoryContext *code_mem;

  // Code execution.
  // Temporary memory pool that is used during the execution. This pool is deleted as soon as the
  // execution completes.
  // MemoryContext *exec_mem;

  // Return status.
  return errcode;
}

}  // namespace sql
}  // namespace yb
