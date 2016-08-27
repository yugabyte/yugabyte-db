//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <iostream>

#include "yb/sql/ybsql.h"
#include "yb/sql/parser/parse_context.h"

namespace yb {
namespace sql {

using std::string;

YbSql::YbSql() : parser_(new Parser()) {
}

YbSql::~YbSql() {
}

int YbSql::Process(const string& sql_stmt) {
  // Temporary memory pool that is used during the parsing process. This pool is deleted as soon as
  // the parsing process is completed.
  MemoryContext *parse_mem = new MemoryContext();

  // Parse tree memory pool. This pool is used to allocate parse tree and its nodes. This pool
  // should be kept until the parse tree is no longer needed.
  MemoryContext *ptree_mem = new MemoryContext();

  // Parse the statement.
  ParseContext parse_ctx(sql_stmt, parse_mem, ptree_mem);

  int errcode = parser_->Parse(&parse_ctx);
  delete parse_mem;
  parse_mem = nullptr;
  if (errcode != ERRCODE_SUCCESSFUL_COMPLETION) {
    return errcode;
  }

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
  delete ptree_mem;
  return errcode;
}

}  // namespace sql.
}  // namespace yb.
