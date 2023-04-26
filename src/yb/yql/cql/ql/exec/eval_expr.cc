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

#include "yb/common/ql_value.h"

#include "yb/util/net/inetaddress.h"

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/util/statement_params.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

Status Executor::PTExprToPB(const PTExpr::SharedPtr& expr, QLExpressionPB *expr_pb) {
  if (expr == nullptr)
    return Status::OK();

  // When selecting from INDEX table, expression's value might be stored in a column.
  const ColumnDesc *index_desc = expr->index_desc();
  if (index_desc) {
    expr_pb->set_column_id(index_desc->id());
    return Status::OK();
  }

  switch (expr->expr_op()) {
    case ExprOperator::kNoOp:
      return Status::OK();

    case ExprOperator::kConst: {
      QLValuePB *const_pb = expr_pb->mutable_value();
      return PTConstToPB(expr, const_pb);
    }

    case ExprOperator::kCollection: {
      // First try to fold literals. Literal-folding is a bit faster than constant folding, so we
      // keep this process although constant-folding can also fold literals.
      QLValuePB *const_pb = expr_pb->mutable_value();
      if (!PTConstToPB(expr, const_pb).ok()) {
        // Use constant folding because literal-folding cannot fold expressions.
        // Example: "List<BLOB>" with function calls.
        //   [ TextAsBlob('a'), IntAsBlob(1) ]
        RETURN_NOT_OK(PTExprToPB(static_cast<const PTCollectionExpr*>(expr.get()), expr_pb));
        return EvalExpr(expr_pb, qlexpr::QLTableRow::empty_row());
      }
      return Status::OK();
    }

    case ExprOperator::kRef:
      return PTExprToPB(static_cast<const PTRef*>(expr.get()), expr_pb);

    case ExprOperator::kSubColRef:
      return PTExprToPB(static_cast<const PTSubscriptedColumn*>(expr.get()), expr_pb);

    case ExprOperator::kJsonOperatorRef:
      return PTExprToPB(static_cast<const PTJsonColumnWithOperators*>(expr.get()), expr_pb);

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

Status Executor::PTExprToPB(const PTBindVar *bind_pt, QLExpressionPB *expr_pb) {
  if (!bind_pt->name()) {
    return STATUS(NotSupported, "Undefined bind variable name, please contact the support");
  }

  QLValue ql_bind;
  DCHECK_NOTNULL(bind_pt->name().get());
  auto status_primary_bindvar = exec_context_->params().GetBindVariable(
      bind_pt->name()->c_str(), bind_pt->pos(), bind_pt->ql_type(), &ql_bind);

  if (status_primary_bindvar.ok()) {
    *expr_pb->mutable_value() = std::move(*ql_bind.mutable_value());
    return Status::OK();
  }

  // Try finding the variable using alternative names.
  for (auto alternative_name : *bind_pt->alternative_names()) {
    auto s = exec_context_->params().GetBindVariable(
        alternative_name->c_str(), bind_pt->pos(), bind_pt->ql_type(), &ql_bind);
    if (s.ok()) {
      *expr_pb->mutable_value() = std::move(*ql_bind.mutable_value());
      return Status::OK();
    }
  }

  LOG(WARNING) << "Bind variable: " << bind_pt->name()
               << " was not found. Status: " << status_primary_bindvar;
  return status_primary_bindvar;
}

//--------------------------------------------------------------------------------------------------

Status Executor::PTExprToPB(const PTRef *ref_pt, QLExpressionPB *ref_pb) {
  // When processing constant folding by client, all columns are not yet accessible, and "PTRef"
  // execution should returns an error to indicate that the folding effort failed.
  const ColumnDesc *col_desc = ref_pt->desc();
  if (!col_desc) {
    // Protection against crash. This happens when compiler failed to analyze a column ref.
    // Example
    //   SELECT [ i ] FROM tab;
    // Expression "[ i ]" should have been analyzed and reported during semantic phase. However,
    // it requires some effort to fix in semantic phase, this error is reported here for now.
    return STATUS(RuntimeError, "Failed to read column value");
  }
  ref_pb->set_column_id(col_desc->id());
  return Status::OK();
}

Status Executor::PTExprToPB(const PTSubscriptedColumn *ref_pt, QLExpressionPB *expr_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  auto col_pb = expr_pb->mutable_subscripted_col();
  col_pb->set_column_id(col_desc->id());
  for (auto& arg : ref_pt->args()->node_list()) {
    RETURN_NOT_OK(PTExprToPB(arg, col_pb->add_subscript_args()));
  }

  return Status::OK();
}

Status Executor::PTExprToPB(const PTJsonColumnWithOperators *ref_pt,
                            QLExpressionPB *expr_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  auto col_pb = expr_pb->mutable_json_column();
  col_pb->set_column_id(col_desc->id());
  for (auto& arg : ref_pt->operators()->node_list()) {
    RETURN_NOT_OK(PTJsonOperatorToPB(std::dynamic_pointer_cast<PTJsonOperator>(arg),
                                     col_pb->add_json_operations()));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::PTExprToPB(const PTAllColumns *ref_pt, QLReadRequestPB *req) {
  QLRSRowDescPB *rsrow_desc_pb = req->mutable_rsrow_desc();
  for (const auto& col_desc : ref_pt->columns()) {
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
