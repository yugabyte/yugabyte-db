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

#include "yb/common/jsonb.h"
#include "yb/common/ql_value.h"

#include "yb/util/result.h"

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_update.h"
#include "yb/yql/cql/ql/util/statement_params.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

Status Executor::ColumnRefsToPB(const PTDmlStmt *tnode,
                                QLReferencedColumnsPB *columns_pb) {
  // Write a list of columns to be read before executing the statement.
  const MCSet<int32>& column_refs = tnode->column_refs();
  for (auto column_ref : column_refs) {
    columns_pb->add_ids(column_ref);
  }

  const MCSet<int32>& static_column_refs = tnode->static_column_refs();
  for (auto column_ref : static_column_refs) {
    columns_pb->add_static_ids(column_ref);
  }
  return Status::OK();
}

Status Executor::ColumnArgsToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req) {
  const MCVector<ColumnArg>& column_args = tnode->column_args();

  for (const ColumnArg& col : column_args) {
    if (!col.IsInitialized()) {
      // This column is not assigned a value, ignore it. We don't support default value yet.
      continue;
    }

    const ColumnDesc *col_desc = col.desc();
    VLOG(3) << "WRITE request, column id = " << col_desc->id();

    const PTExpr::SharedPtr& expr = col.expr();
    if (expr != nullptr && expr->expr_op() == ExprOperator::kBindVar) {
      const PTBindVar* bind_pt = static_cast<const PTBindVar*>(expr.get());
      DCHECK_NOTNULL(bind_pt->name().get());
      if(VERIFY_RESULT(exec_context_->params().IsBindVariableUnset(bind_pt->name()->c_str(),
                                                                   bind_pt->pos()))) {
        VLOG(3) << "Value unset for column: " << bind_pt->name()->c_str();
        if (col_desc->is_primary()) {
          VLOG(3) << "Unexpected value unset for primary key. Current request: "
                  << req->DebugString();
          return exec_context_->Error(tnode, ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
        }

        continue;
      }
    }

    QLExpressionPB *expr_pb = CreateQLExpression(req, *col_desc);

    RETURN_NOT_OK(PTExprToPB(expr, expr_pb));

    if (col_desc->is_primary()) {
      RETURN_NOT_OK(EvalExpr(expr_pb, qlexpr::QLTableRow::empty_row()));
    }

    // Null values not allowed for primary key: checking here catches nulls introduced by bind.
    if (col_desc->is_primary() && expr_pb->has_value() && IsNull(expr_pb->value())) {
      LOG(INFO) << "Unexpected null value. Current request: " << req->DebugString();
      return exec_context_->Error(tnode, ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }

  const MCVector<SubscriptedColumnArg>& subcol_args = tnode->subscripted_col_args();
  for (const SubscriptedColumnArg& col : subcol_args) {
    const ColumnDesc *col_desc = col.desc();
    QLColumnValuePB *col_pb = req->add_column_values();
    col_pb->set_column_id(col_desc->id());
    QLExpressionPB *expr_pb = col_pb->mutable_expr();
    RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
    for (auto& col_arg : col.args()->node_list()) {
      QLExpressionPB *arg_pb = col_pb->add_subscript_args();
      RETURN_NOT_OK(PTExprToPB(col_arg, arg_pb));
    }
  }

  const MCVector<JsonColumnArg>& jsoncol_args = tnode->json_col_args();
  for (const JsonColumnArg& col : jsoncol_args) {
    QLExpressionPB expr_pb;
    RETURN_NOT_OK(PTExprToPB(col.expr(), &expr_pb));

    if (tnode->opcode() == TreeNodeOpcode::kPTUpdateStmt) {
      const PTUpdateStmt* update_tnode = static_cast<const PTUpdateStmt*>(tnode);
      if (update_tnode->update_properties() &&
          update_tnode->update_properties()->ignore_null_jsonb_attributes()) {
        if (expr_pb.expr_case() == QLExpressionPB::kValue &&
            expr_pb.value().value_case() == QLValuePB::kJsonbValue &&
            expr_pb.value().jsonb_value() == common::Jsonb::kSerializedJsonbNull) {
          // TODO(Piyush): Log attribute json path as well.
          VLOG(1) << "Ignoring null for json attribute in UPDATE statement " \
            "for column " << col.desc()->MangledName();
          continue;
        }
      }
    }

    const ColumnDesc *col_desc = col.desc();
    QLColumnValuePB *col_pb = req->add_column_values();
    col_pb->set_column_id(col_desc->id());
    *(col_pb->mutable_expr()) = expr_pb;

    for (auto& col_arg : col.args()->node_list()) {
      QLJsonOperationPB *arg_pb = col_pb->add_json_args();
      RETURN_NOT_OK(PTJsonOperatorToPB(std::dynamic_pointer_cast<PTJsonOperator>(col_arg), arg_pb));
    }
  }

  return Status::OK();
}

}  // namespace ql
}  // namespace yb
