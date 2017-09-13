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

#include "yb/sql/exec/exec_context.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace sql {

ExecContext::ExecContext(const char *sql_stmt,
                         size_t stmt_len,
                         const ParseTree *parse_tree,
                         const StatementParameters *params,
                         SqlEnv *sql_env)
    : ProcessContextBase(sql_stmt, stmt_len),
      parse_tree_(parse_tree),
      params_(params),
      start_time_(MonoTime::Now(MonoTime::FINE)),
      sql_env_(sql_env) {
}

ExecContext::~ExecContext() {
}

Status ExecContext::Error(ErrorCode error_code) {
  return ProcessContextBase::Error(tnode(), error_code);
}

Status ExecContext::Error(const char *m, ErrorCode error_code) {
  return ProcessContextBase::Error(tnode(), m, error_code);
}

Status ExecContext::Error(const Status& s, ErrorCode error_code) {
  return ProcessContextBase::Error(tnode(), s, error_code);
}

}  // namespace sql
}  // namespace yb
