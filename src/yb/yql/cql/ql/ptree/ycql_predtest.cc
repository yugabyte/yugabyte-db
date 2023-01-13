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

#include "yb/yql/cql/ql/ptree/ycql_predtest.h"

#include "yb/common/common.pb.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"

using std::vector;

namespace yb {
namespace ql {

// TODO(Piyush): Make WhereClauseImpliesPred() more smarter -
//   1. Support more complex math rules: e.g.,: v > 5 => v > 4. Right now we don't handle that.

// TODO (Piyush): To support WhereClauseImpliesPred() for arbitrary expressions, we need
// PTConstToPB() and that needs some refactor to be able to use here. For now we are checking only
// for expressions with NULLs, integers, booleans and strings.

static Result<bool> exprMatchesColOp(const ColumnOp& col_op, const QLExpressionPB& predicate) {
  int col_id_in_pred = predicate.condition().operands(0).column_id();
  QLValuePB value_in_pred = predicate.condition().operands(1).value();

  if (col_op.yb_op() != predicate.condition().op()) return false;
  if (col_op.desc()->id() != col_id_in_pred) return false;
  if (col_op.expr()->expr_op() != ExprOperator::kConst) return false;

  // We support only NULLs and int in index predicates for now.
  switch (value_in_pred.value_case()) {
    case QLValuePB::VALUE_NOT_SET: {
      std::shared_ptr<PTNull> const_null_value =
        std::dynamic_pointer_cast<PTNull>(col_op.expr());
      if (const_null_value) return true;
      return false;
    }
    case QLValuePB::kInt64Value: {
      std::shared_ptr<PTConstInt> const_int_value =
        std::dynamic_pointer_cast<PTConstInt>(col_op.expr());
      if (const_int_value && const_int_value->value() == value_in_pred.int64_value())
        return true;

      int64_t value;
      std::shared_ptr<PTConstVarInt> const_varint_value =
        std::dynamic_pointer_cast<PTConstVarInt>(col_op.expr());
      if (const_varint_value) {
        if (!const_varint_value->ToInt64(&value, false).ok()) return false;
        if (value == value_in_pred.int64_value()) return true;
      }
      return false;
    }
    case QLValuePB::kInt32Value: {
      std::shared_ptr<PTConstInt> const_int_value =
        std::dynamic_pointer_cast<PTConstInt>(col_op.expr());
      if (const_int_value && const_int_value->value() == value_in_pred.int32_value())
        return true;

      int64_t value;
      std::shared_ptr<PTConstVarInt> const_varint_value =
        std::dynamic_pointer_cast<PTConstVarInt>(col_op.expr());
      if (const_varint_value) {
        if (!const_varint_value->ToInt64(&value, false).ok()) return false;
        if (value == value_in_pred.int32_value()) return true;
      }
      return false;
    }
    case QLValuePB::kInt16Value: {
      std::shared_ptr<PTConstInt> const_int_value =
        std::dynamic_pointer_cast<PTConstInt>(col_op.expr());
      if (const_int_value && const_int_value->value() == value_in_pred.int16_value())
        return true;

      int64_t value;
      std::shared_ptr<PTConstVarInt> const_varint_value =
        std::dynamic_pointer_cast<PTConstVarInt>(col_op.expr());
      if (const_varint_value) {
        if (!const_varint_value->ToInt64(&value, false).ok()) return false;
        if (value == value_in_pred.int16_value()) return true;
      }
      return false;
    }
    case QLValuePB::kInt8Value: {
      std::shared_ptr<PTConstInt> const_int_value =
        std::dynamic_pointer_cast<PTConstInt>(col_op.expr());
      if (const_int_value && const_int_value->value() == value_in_pred.int8_value())
        return true;

      int64_t value;
      std::shared_ptr<PTConstVarInt> const_varint_value =
        std::dynamic_pointer_cast<PTConstVarInt>(col_op.expr());
      if (const_varint_value) {
        if (!const_varint_value->ToInt64(&value, false).ok()) return false;
        if (value == value_in_pred.int8_value()) return true;
      }
      return false;
    }
    case QLValuePB::kBoolValue: {
      std::shared_ptr<PTConstBool> const_bool_value =
        std::dynamic_pointer_cast<PTConstBool>(col_op.expr());
      if (const_bool_value && const_bool_value->value() == value_in_pred.bool_value())
        return true;

      return false;
    }
    case QLValuePB::kStringValue: {
      std::shared_ptr<PTConstText> const_text_value =
        std::dynamic_pointer_cast<PTConstText>(col_op.expr());
      if (const_text_value && const_text_value->ToString() == value_in_pred.string_value())
        return true;

      return false;
    }
    default:
      return false;
  }
}
// Check if all ops in predicate are included in the col_id_ops_map i.e., whether each sub-clause
// in the predicate matches some op in col_id_ops_map. Since sub-clauses in predicate can only be
// connected by the AND operator - if this function returns true, it means all ops in col_id_ops_map
// if connected by AND, logically imply the predicate.
//
// Currently we are only able to check a match for ops which have =, !=, >, >=, <, <= operators on
// columns of the following type (if operator is applicable to the type) - TINYINT, SMALLINT, INT,
// BIGINT, VARINT, BOOL and TEXT.
static Result<bool> ExprInOps(const QLExpressionPB& predicate,
                              const std::unordered_map<int,
                                std::vector<const ColumnOp*>> &col_id_ops_map,
                              int *pred_len,
                              MCUnorderedMap<int32, uint16> *column_ref_cnts) {
  if (!predicate.has_condition()) {
    RSTATUS_DCHECK(false, InternalError, "Complex expressions not supported in index predicate."
      " Should have been blocked in index create itself");
  }

  switch (predicate.condition().op()) {
    case QL_OP_AND:
      return
        VERIFY_RESULT(ExprInOps(predicate.condition().operands(0), col_id_ops_map, pred_len,
                                column_ref_cnts)) &&
        VERIFY_RESULT(ExprInOps(predicate.condition().operands(1), col_id_ops_map, pred_len,
                                column_ref_cnts));
    case QL_OP_EQUAL:
    case QL_OP_NOT_EQUAL:
    case QL_OP_GREATER_THAN:
    case QL_OP_GREATER_THAN_EQUAL:
    case QL_OP_LESS_THAN_EQUAL:
    case QL_OP_LESS_THAN: {
      DCHECK(predicate.condition().operands(0).has_column_id()) <<
        "Complex expressions not supported in index predicate";
      DCHECK(predicate.condition().operands(1).has_value()) <<
        "Complex expressions not supported in index predicate";

      *pred_len = *pred_len + 1;
      int col_id_in_pred = predicate.condition().operands(0).column_id();

      if (col_id_ops_map.find(col_id_in_pred) == col_id_ops_map.end())
        return false;

      for (const ColumnOp* col_op : col_id_ops_map.at(col_id_in_pred)) {
        if (VERIFY_RESULT(exprMatchesColOp(*col_op, predicate))) {
          if (column_ref_cnts->find(col_op->desc()->id()) != column_ref_cnts->end()) {
            (*column_ref_cnts)[col_op->desc()->id()]--;
            // TODO(Piyush): Handle overflow errors
            // if (overflow)
            //  RSTATUS_DCHECK(false, InternalError, "Invalid state");
          }
          return true;
        }
      }
      return false;
    }
    default:
      RSTATUS_DCHECK(false, InternalError, "Complex expressions not supported in index predicate."
        " Should have been blocked in index create itself");
  }
}

Result<bool> WhereClauseImpliesPred(const MCList<ColumnOp> &col_ops,
                                    const QLExpressionPB& predicate,
                                    int *predicate_len,
                                    MCUnorderedMap<int32, uint16> *column_ref_cnts) {
  // Column Id to op map
  std::unordered_map<int, std::vector<const ColumnOp*>> col_id_ops_map;

  for (const ColumnOp& col_op : col_ops) {
    int col_id = col_op.desc()->id();
    if (col_id_ops_map.find(col_id) == col_id_ops_map.end())
      col_id_ops_map[col_id] = vector<const ColumnOp*>();
    col_id_ops_map[col_id].push_back(&col_op);
  }

  return ExprInOps(predicate, col_id_ops_map, predicate_len, column_ref_cnts);
}

Result<bool> OpInExpr(const QLExpressionPB& predicate, const ColumnOp& col_op) {
  if (!predicate.has_condition()) {
    RSTATUS_DCHECK(false, InternalError, "Complex expressions not supported in index predicate."
      " Should have been blocked in index create itself");
  }

  switch (predicate.condition().op()) {
    case QL_OP_AND:
      return
        VERIFY_RESULT(OpInExpr(predicate.condition().operands(0), col_op)) ||
        VERIFY_RESULT(OpInExpr(predicate.condition().operands(1), col_op));
    case QL_OP_EQUAL:
    case QL_OP_NOT_EQUAL:
    case QL_OP_GREATER_THAN:
    case QL_OP_GREATER_THAN_EQUAL:
    case QL_OP_LESS_THAN_EQUAL:
    case QL_OP_LESS_THAN: {
      DCHECK(predicate.condition().operands(0).has_column_id()) <<
        "Complex expressions not supported in index predicate";
      DCHECK(predicate.condition().operands(1).has_value()) <<
        "Complex expressions not supported in index predicate";
      if (VERIFY_RESULT(exprMatchesColOp(col_op, predicate))) return true;
      return false;
    }
    default:
      RSTATUS_DCHECK(false, InternalError, "Complex expressions not supported in index predicate. "
        "Should have been blocked in index create itself");
  }
}

}  // namespace ql
}  // namespace yb
