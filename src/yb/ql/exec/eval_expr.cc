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

#include "yb/ql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/util/date_time.h"
#include "yb/util/net/inetaddress.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

Status Executor::PTExprToPB(const PTExpr::SharedPtr& expr, QLExpressionPB *expr_pb) {
  if (expr == nullptr)
    return Status::OK();
  switch (expr->expr_op()) {
    case ExprOperator::kNoOp:
      return Status::OK();

    case ExprOperator::kConst:
    case ExprOperator::kCollection: {
      QLValuePB *const_pb = expr_pb->mutable_value();
      return PTConstToPB(expr, const_pb);
    }

    case ExprOperator::kRef:
      return PTExprToPB(static_cast<const PTRef*>(expr.get()), expr_pb);

    case ExprOperator::kSubColRef:
      return PTExprToPB(static_cast<const PTSubscriptedColumn*>(expr.get()), expr_pb);

    case ExprOperator::kBindVar:
      return PTExprToPB(static_cast<const PTBindVar*>(expr.get()), expr_pb);

    case ExprOperator::kAlias:
      return PTExprToPB(expr->op1(), expr_pb);

    case ExprOperator::kUMinus:
      return PTUMinusToPB(static_cast<const PTOperator1*>(expr.get()), expr_pb);

    case ExprOperator::kBcall:
      return PTExprToPB(static_cast<const PTBcall*>(expr.get()), expr_pb);

    case ExprOperator::kLogic1:
      return PTExprToPB(static_cast<const PTLogic1*>(expr.get()), expr_pb);

    case ExprOperator::kLogic2:
      return PTExprToPB(static_cast<const PTLogic2*>(expr.get()), expr_pb);

    case ExprOperator::kRelation0:
      return PTExprToPB(static_cast<const PTRelation0*>(expr.get()), expr_pb);

    case ExprOperator::kRelation1:
      return PTExprToPB(static_cast<const PTRelation1*>(expr.get()), expr_pb);

    case ExprOperator::kRelation2:
      return PTExprToPB(static_cast<const PTRelation2*>(expr.get()), expr_pb);

    case ExprOperator::kRelation3:
      return PTExprToPB(static_cast<const PTRelation3*>(expr.get()), expr_pb);

    default:
      LOG(FATAL) << "Not supported operator" << static_cast<int>(expr->expr_op());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::PTExprToPB(const PTBindVar *bind_pt, QLExpressionPB *expr_pb) {
  // TODO(neil) This error should be raised by CQL when it compares between bind variables and
  // bind arguments before calling QL layer to execute.
  if (exec_context_->params() == nullptr) {
    return STATUS(RuntimeError, "no bind variable supplied");
  }

  QLValue ql_bind;
  RETURN_NOT_OK(exec_context_->params()->GetBindVariable(bind_pt->name()->c_str(),
                                                         bind_pt->pos(),
                                                         bind_pt->ql_type(),
                                                         &ql_bind));
  expr_pb->mutable_value()->Swap(ql_bind.mutable_value());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::PTExprToPB(const PTRef *ref_pt, QLExpressionPB *ref_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  ref_pb->set_column_id(col_desc->id());
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTSubscriptedColumn *ref_pt, QLExpressionPB *expr_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  auto col_pb = expr_pb->mutable_subscripted_col();
  col_pb->set_column_id(col_desc->id());
  for (auto& arg : ref_pt->args()->node_list()) {
    RETURN_NOT_OK(PTExprToPB(arg, col_pb->add_subscript_args()));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::PTExprToPB(const PTAllColumns *ref_pt, QLReadRequestPB *req) {
  QLRSRowDescPB *rsrow_desc_pb = req->mutable_rsrow_desc();
  for (const auto& col_desc : ref_pt->table_columns()) {
    req->add_selected_exprs()->set_column_id(col_desc.id());

    // Add the expression metadata (rsrow descriptor).
    QLRSColDescPB *rscol_descs_pb = rsrow_desc_pb->add_rscol_descs();
    rscol_descs_pb->set_name(col_desc.name());
    col_desc.ql_type()->ToQLTypePB(rscol_descs_pb->mutable_ql_type());
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
