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

#include <string>

#include "yb/yql/pgsql/pbgen/pg_coder.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TExprToPB(const PgTExpr::SharedPtr& expr, PgsqlExpressionPB *expr_pb) {
  if (expr == nullptr)
    return Status::OK();

  switch (expr->expr_op()) {
    case ExprOperator::kNoOp:
      return Status::OK();

    case ExprOperator::kConst: {
      QLValuePB *const_pb = expr_pb->mutable_value();
      return TConstToPB(expr, const_pb);
    }

    case ExprOperator::kBcall:
      return TExprToPB(static_cast<const PgTBcall*>(expr.get()), expr_pb);

    case ExprOperator::kRef:
      return TExprToPB(static_cast<const PgTRef*>(expr.get()), expr_pb);

    default:
      LOG(FATAL) << "Not supported operator" << static_cast<int>(expr->expr_op());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TExprToPB(const PgTRef *ref_pt, PgsqlExpressionPB *ref_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  ref_pb->set_column_id(col_desc->id());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TExprToPB(const PgTAllColumns *ref_pt, PgsqlReadRequestPB *req) {
  PgsqlRSRowDescPB *rsrow_desc_pb = req->mutable_rsrow_desc();
  for (const auto& col_desc : ref_pt->table_columns()) {
    if (col_desc.order() < ColumnDesc::kHiddenColumnCount) {
      // Skip all internal columns as users do not expect to see them.
      continue;
    }
    req->add_selected_exprs()->set_column_id(col_desc.id());

    // Add the expression metadata (rsrow descriptor).
    PgsqlRSColDescPB *rscol_descs_pb = rsrow_desc_pb->add_rscol_descs();
    rscol_descs_pb->set_name(col_desc.name());
    col_desc.ql_type()->ToQLTypePB(rscol_descs_pb->mutable_ql_type());
  }
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
