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
// Tree node definitions for INSERT INTO ... JSON clause.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/ptree/pt_insert_json_clause.h"

#include <boost/optional.hpp>

#include "yb/common/ql_type.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

namespace yb {
namespace ql {

PTInsertJsonClause::PTInsertJsonClause(MemoryContext* memctx,
                                       const YBLocation::SharedPtr& loc,
                                       const PTExpr::SharedPtr& json_expr,
                                       bool default_null)
    : PTCollection(memctx, loc),
      default_null_(default_null),
      json_expr_(json_expr),
      json_string_(""),
      json_document_(boost::none) {}

PTInsertJsonClause::~PTInsertJsonClause() = default;

Status PTInsertJsonClause::Analyze(SemContext* sem_context) {
  SemState sem_state(sem_context, QLType::Create(DataType::STRING), InternalType::kStringValue);
  sem_state.set_bindvar_name("json"); // To match Cassandra behaviour
  RETURN_NOT_OK(json_expr_->Analyze(sem_context));
  // Most of the work will be done in pre-exec phase
  return Status::OK();
}

void PTInsertJsonClause::PrintSemanticAnalysisResult(SemContext* sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace ql
}  // namespace yb
