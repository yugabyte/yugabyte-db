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
// This file contains common code for evaluating YQLExpressions that may be used by client or server
// TODO (Mihnea or Neil) This should be integrated into YQLExprExecutor when or after support for
// selecting expressions is done.
//--------------------------------------------------------------------------------------------------

#include "yb/common/yql_expression.h"

namespace yb {

// TODO(neil) When memory pool is implemented in DocDB, we should run some perf tool to optimize
// the expression evaluating process. The intermediate / temporary YQLValue should be allocated
// in the pool. Currently, the argument structures are on stack, but their contents are in the
// heap memory.
CHECKED_STATUS YQLExpression::Evaluate(const QLExpressionPB &ql_expr,
                                       const QLTableRow &table_row,
                                       QLValueWithPB *result,
                                       WriteAction *write_action) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:
      result->Assign(ql_expr.value());
      break;

    case QLExpressionPB::ExprCase::kBfcall: {
      const QLBCallPB &bfcall = ql_expr.bfcall();
      const bfql::BFOperator::SharedPtr bf_op = bfql::kBFOperators[bfcall.opcode()];
      const string &bfop_name = bf_op->op_decl()->cpp_name();

      // Special cases: for collection operations of the form "cref = cref +/- <value>" we avoid
      // reading column cref and instead tell doc writer to modify it in-place
      if (bfop_name == "AddMapMap" || bfop_name == "AddSetSet") {
        *write_action = WriteAction::EXTEND;
        return Evaluate(bfcall.operands(1), table_row, result, write_action);
      }

      if (bfop_name == "SubMapSet" || bfop_name == "SubSetSet") {
        *write_action = WriteAction::REMOVE_KEYS;
        return Evaluate(bfcall.operands(1), table_row, result, write_action);
      }

      if (bfop_name == "AddListList") {
        if (bfcall.operands(0).has_column_id()) {
          *write_action = WriteAction::APPEND;
          return Evaluate(bfcall.operands(1), table_row, result, write_action);
        } else {
          *write_action = WriteAction::PREPEND;
          return Evaluate(bfcall.operands(0), table_row, result, write_action);
        }
      }

      // TODO (Akashnil or Mihnea) this should be enabled when RemoveFromList is implemented
      /*
      if (bfop_name == "SubListList") {
        *write_action = WriteAction::REMOVE_VALUES;
        return Evaluate(bfcall.operands(1), table_row, result, write_action);
      }
      */

      // Default case: First evaluate the arguments.
      vector <QLValueWithPB> args(bfcall.operands().size());
      int arg_index = 0;
      for (auto operand : bfcall.operands()) {
        RETURN_NOT_OK(Evaluate(operand, table_row, &args[arg_index], write_action));
        arg_index++;
      }

      // Execute the builtin call associated with the given opcode.
      QLBfunc::Exec(static_cast<bfql::BFOpcode>(bfcall.opcode()), &args, result);
      break;
    }

    case QLExpressionPB::ExprCase::kColumnId: {
      auto iter = table_row.find(ColumnId(ql_expr.column_id()));
      if (iter != table_row.end()) {
        result->Assign(iter->second.value);
      } else {
        result->SetNull();
      }
      break;
    }

      // Cases below should have been caught by the analyzer so we just Fatal here.
      // TODO When this is integrated into QLExprExecutor some invariants might change here.
    case QLExpressionPB::ExprCase::kSubscriptedCol:
      LOG(FATAL) << "Internal error: Subscripted column is not allowed in this context";
      break;

    case QLExpressionPB::ExprCase::kCondition:
      LOG(FATAL) << "Internal error: Conditional expression is not allowed in this context";
      break;

    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      LOG(FATAL) << "Internal error: Null/Unset expression is not allowed in this context";
      break;

    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kTscall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBocall:
      LOG(FATAL) << "Internal error: Not yet supported";
  }
  return Status::OK();
}

} // namespace yb
