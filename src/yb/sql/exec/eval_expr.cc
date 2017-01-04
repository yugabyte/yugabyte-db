//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/eval_expr.h"
#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace sql {

Status Executor::EvalExpr(const PTExpr::SharedPtr& expr, EvalValue *result) {

  switch (expr->type_id()) {
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::INT32: FALLTHROUGH_INTENDED;
    case DataType::INT64: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalIntExpr(expr, &int_value));
      RETURN_NOT_OK(ConvertFromInt(result, int_value));
      break;
    }

    case DataType::FLOAT: FALLTHROUGH_INTENDED;
    case DataType::DOUBLE: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalDoubleExpr(expr, &double_value));
      RETURN_NOT_OK(ConvertFromDouble(result, double_value));
      break;
    }

    case DataType::STRING: {
      EvalStringValue string_value;
      RETURN_NOT_OK(EvalStringExpr(expr, &string_value));
      RETURN_NOT_OK(ConvertFromString(result, string_value));
      break;
    }

    case DataType::BOOL: {
      EvalBoolValue bool_value;
      RETURN_NOT_OK(EvalBoolExpr(expr, &bool_value));
      RETURN_NOT_OK(ConvertFromBool(result, bool_value));
      break;
    }

    default:
      LOG(FATAL) << "Unknown expression datatype";
  }

  return Status::OK();
}

Status Executor::EvalIntExpr(const PTExpr::SharedPtr& expr, EvalIntValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstInt*>(e)->Eval();
      break;

    default:
      LOG(FATAL) << "Not supported operator";
  }
  return eval_status;
}

Status Executor::EvalDoubleExpr(const PTExpr::SharedPtr& expr, EvalDoubleValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstDouble*>(e)->Eval();
      break;

    default:
      LOG(FATAL) << "Not supported operator";
  }
  return eval_status;
}

Status Executor::EvalStringExpr(const PTExpr::SharedPtr& expr, EvalStringValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstText*>(e)->Eval();
      break;

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::EvalBoolExpr(const PTExpr::SharedPtr& expr, EvalBoolValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstBool*>(e)->Eval();
      break;

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::ConvertFromInt(EvalValue *result, const EvalIntValue& int_value) {
  switch (result->datatype()) {
    case DataType::INT64:
      static_cast<EvalIntValue *>(result)->value_ = int_value.value_;
      break;

    case DataType::DOUBLE:
      static_cast<EvalDoubleValue *>(result)->value_ = int_value.value_;
      break;

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

Status Executor::ConvertFromDouble(EvalValue *result, const EvalDoubleValue& double_value) {
  switch (result->datatype()) {
    case DataType::DOUBLE:
      static_cast<EvalDoubleValue *>(result)->value_ = double_value.value_;
      break;

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

Status Executor::ConvertFromString(EvalValue *result, const EvalStringValue& string_value) {
  switch (result->datatype()) {
    case DataType::STRING:
      static_cast<EvalStringValue *>(result)->value_ = string_value.value_;
      break;

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

Status Executor::ConvertFromBool(EvalValue *result, const EvalBoolValue& bool_value) {
  switch (result->datatype()) {
    case DataType::BOOL:
      static_cast<EvalBoolValue *>(result)->value_ = bool_value.value_;
      break;

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
