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
//
// Tree node definitions for INSERT INTO ... VALUES clause.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_insert_values_clause.h"

#include "yb/yql/cql/ql/ptree/pt_expr.h"

#include "yb/yql/cql/ql/ptree/yb_location.h"

namespace yb {
namespace ql {

PTInsertValuesClause::PTInsertValuesClause(MemoryContext* memctx,
                                           YBLocationPtr loc,
                                           PTExprListNode::SharedPtr tuple)
    : PTCollection(memctx, loc),
      tuples_(memctx, loc) {
  Append(tuple);
}

PTInsertValuesClause::~PTInsertValuesClause() = default;

void PTInsertValuesClause::Append(const PTExprListNode::SharedPtr& tuple) {
  tuples_.Append(tuple);
}

void PTInsertValuesClause::Prepend(const PTExprListNode::SharedPtr& tuple) {
  tuples_.Prepend(tuple);
}

Status PTInsertValuesClause::Analyze(SemContext* sem_context) {
  return Status::OK();
}

void PTInsertValuesClause::PrintSemanticAnalysisResult(SemContext* sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

PTExprListNode::SharedPtr PTInsertValuesClause::Tuple(int index) const {
  DCHECK_GE(index, 0);
  return tuples_.element(index);
}

}  // namespace ql
}  // namespace yb
