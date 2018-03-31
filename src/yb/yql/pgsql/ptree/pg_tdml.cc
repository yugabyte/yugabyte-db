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
// Treenode implementation for DML including SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_compile_context.h"
#include "yb/yql/pgsql/ptree/pg_tdml.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"
#include "yb/common/table_properties_constants.h"

namespace yb {
namespace pgsql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBTableName;
using client::YBColumnSchema;

const PgTExpr::SharedPtr PgTDmlStmt::kNullPointerRef = nullptr;

PgTDmlStmt::PgTDmlStmt(MemoryContext *memctx,
                       PgTLocation::SharedPtr loc,
                       PgTExpr::SharedPtr where_clause)
  : PgTCollection(memctx, loc),
    is_system_(false),
    table_columns_(memctx),
    num_key_columns_(0),
    oid_name_(memctx),
    ctid_name_(memctx),
    func_ops_(memctx),
    key_where_ops_(memctx),
    where_ops_(memctx),
    partition_key_ops_(memctx),
    where_clause_(where_clause),
    column_args_(nullptr),
    column_refs_(memctx),
    selected_schemas_(nullptr) {
  // Create a fake treenode for oid argument here.
  // This arg can be an inserting value or an argument in WHERE clause.
  oid_name_ = "oid";
  oid_arg_ = PgTConstInt::MakeShared(memctx, loc, 0);
  oid_arg_->set_expected_internal_type(InternalType::kInt64Value);

  // CTID column.
  ctid_name_ = "ctid";
  MCSharedPtr<MCString> now_func = MCMakeShared<MCString>(memctx, "now");
  PgTExprListNode::SharedPtr now_args = PgTExprListNode::MakeShared(memctx, loc);
  ctid_arg_ = PgTBcall::MakeShared(memctx, loc, now_func, now_args);
  ctid_arg_->set_expected_internal_type(InternalType::kTimeuuidValue);
}

PgTDmlStmt::~PgTDmlStmt() {
}

CHECKED_STATUS PgTDmlStmt::LookupTable(PgCompileContext *compile_context) {
  int num_partition_columns = 0;
  RETURN_NOT_OK(compile_context->LookupTable(table_name(),
                                             table_loc(),
                                             IsWriteOp(),
                                             &table_,
                                             &is_system_,
                                             &table_columns_,
                                             &num_key_columns_,
                                             &num_partition_columns));
  CHECK_EQ(num_partition_columns, num_partition_columns_);
  return Status::OK();
}

// Node semantics analysis.
CHECKED_STATUS PgTDmlStmt::Analyze(PgCompileContext *compile_context) {
  compile_context->set_current_dml_stmt(this);
  MemoryContext *psem_mem = compile_context->PSemMem();
  column_args_ = MCMakeShared<MCVector<ColumnArg>>(psem_mem);
  return Status::OK();
}

CHECKED_STATUS PgTDmlStmt::AnalyzeWhereClause(PgCompileContext *compile_context,
                                              const PgTExpr::SharedPtr& where_clause) {
  if (where_clause == nullptr) {
    return Status::OK();
  }

  // Analyze where expression.
  return AnalyzeWhereExpr(compile_context, where_clause.get());
}

CHECKED_STATUS PgTDmlStmt::AnalyzeWhereExpr(PgCompileContext *compile_context, PgTExpr *expr) {
  // Construct the state variables and analyze the expression.
  PgSemState sem_state(compile_context, QLType::Create(BOOL), InternalType::kBoolValue);

  // TODO(neil) Import Postgresql code for where clause here.
  // - After or during analyzing expressions in where clause, we need to intelligently extract the
  //   RANGE columns into the following format.
  //     KEY_1_cond && [ KEY_2_cond && ... ] && REGULAR_COND
  //   This is important for the backend to optimize the filtering process. The key columns would be
  //   used to compute the range indexes to select requested rows efficiently.
  //
  // - For now, we'll skip this step, which will trigger a scanning effort.
  RETURN_NOT_OK(expr->Analyze(compile_context));

  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
