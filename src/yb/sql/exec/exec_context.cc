//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/exec_context.h"

namespace yb {
namespace sql {

ExecContext::ExecContext(const char *sql_stmt,
                         size_t stmt_len,
                         ParseTree::UniPtr parse_tree,
                         SqlEnv *sql_env)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      sql_env_(sql_env) {
}

ExecContext::~ExecContext() {
}

}  // namespace sql
}  // namespace yb
