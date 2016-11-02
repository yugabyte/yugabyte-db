//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/exec_context.h"

namespace yb {
namespace sql {

ExecContext::ExecContext(const char *sql_stmt,
                         size_t stmt_len,
                         ParseTree::UniPtr parse_tree,
                         SessionContext *session_context)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      session_context_(session_context) {
}

ExecContext::~ExecContext() {
}

}  // namespace sql
}  // namespace yb
