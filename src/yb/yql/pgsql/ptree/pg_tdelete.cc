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
// Treenode implementation for DELETE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_tdelete.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

PgTDeleteStmt::PgTDeleteStmt(MemoryContext *memctx,
                             PgTLocation::SharedPtr loc,
                             PgTTableRef::SharedPtr relation,
                             PgTExpr::SharedPtr where_clause)
    : PgTDmlStmt(memctx, loc, where_clause) {
}

PgTDeleteStmt::~PgTDeleteStmt() {
}

CHECKED_STATUS PgTDeleteStmt::Analyze(PgCompileContext *compile_context) {

  RETURN_NOT_OK(PgTDmlStmt::Analyze(compile_context));

  RETURN_NOT_OK(relation_->Analyze(compile_context));

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(compile_context));

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(compile_context, where_clause_));

  column_args_->resize(num_columns());
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
