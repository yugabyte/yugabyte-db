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
#include "yb/yql/cql/ql/parser/parse_context.h"

#include <stdio.h>

#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"

namespace yb {
namespace ql {

using std::istream;
using std::min;
using std::string;

//--------------------------------------------------------------------------------------------------
// ParseContext
//--------------------------------------------------------------------------------------------------

ParseContext::ParseContext(const string& stmt,
                           const bool reparsed,
                           const MemTrackerPtr& mem_tracker,
                           const bool internal)
    : ProcessContext(std::make_unique<ParseTree>(stmt, reparsed, mem_tracker, internal)),
      bind_variables_(PTreeMem()),
      stmt_offset_(0),
      trace_scanning_(false),
      trace_parsing_(false) {

  // The MAC version of FLEX requires empty or valid input stream. It does not allow input file to
  // be nullptr.
  ql_file_ = std::make_unique<istream>(nullptr);

  if (VLOG_IS_ON(3)) {
    trace_scanning_ = true;
    trace_parsing_ = true;
  }
}

ParseContext::~ParseContext() {
}

//--------------------------------------------------------------------------------------------------

size_t ParseContext::Read(char* buf, size_t max_size) {
  const size_t copy_size = min<size_t>(stmt().length() - stmt_offset_, max_size);
  if (copy_size > 0) {
    stmt().copy(buf, copy_size, stmt_offset_);
    stmt_offset_ += copy_size;
    return copy_size;
  }
  return 0;
}

void ParseContext::GetBindVariables(MCVector<PTBindVar*> *vars) {
  vars->clear();
  for (auto it = bind_variables_.cbegin(); it != bind_variables_.cend(); it++) {
    PTBindVar *var = *it;
    // Set the ordinal position of the bind variable in the statement also.
    if (var->is_unset_pos()) {
      var->set_pos(bind_pos_);
    }
    vars->push_back(var);
    bind_pos_++;
  }
  // Once the current statement has copied the bind variables found in it, clear the bind vars
  // before we process the next statement.
  bind_variables_.clear();
}

void ParseContext::Warn(const location& loc, const char *msg, ErrorCode error_code) {
  ProcessContext::Warn(Location(loc), msg, error_code);
}

// Handling parsing error.
Status ParseContext::Error(const location& loc,
                           const char *msg,
                           ErrorCode error_code,
                           const char* token) {
  return ProcessContext::Error(Location(loc), msg, error_code, token);
}

Status ParseContext::Error(const location& loc, const char *msg, const char* token) {
  return ProcessContext::Error(Location(loc), msg, token);
}

Status ParseContext::Error(const location& loc, ErrorCode error_code, const char* token) {
  return ProcessContext::Error(Location(loc), error_code, token);
}

bool ParseContext::SetCmp::operator()(const PTBindVar* v1, const PTBindVar* v2) const {
  const YBLocation& l1 = v1->loc();
  const YBLocation& l2 = v2->loc();
  return (l1.BeginLine() < l2.BeginLine() ||
          (l1.BeginLine() == l2.BeginLine() && l1.BeginColumn() < l2.BeginColumn()));
}

}  // namespace ql
}  // namespace yb
