//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/eval_expr.h"
#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/util/date_time.h"

namespace yb {
namespace sql {

Status Executor::GetBindVariable(const PTBindVar* var, YQLValue *value) const {
  return params_->GetBindVariable(string(var->name()->c_str()),
                                  var->pos(),
                                  var->sql_type(),
                                  value);
}

Status Executor::EvalExpr(const PTExpr::SharedPtr& expr, EvalValue *result) {

  switch (expr->type_id()) {
    case InternalType::VALUE_NOT_SET: {
      // This is a null node.
      result->set_null();
      break;
    }

    case InternalType::kInt8Value: FALLTHROUGH_INTENDED;
    case InternalType::kInt16Value: FALLTHROUGH_INTENDED;
    case InternalType::kInt32Value: FALLTHROUGH_INTENDED;
    case InternalType::kInt64Value: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalIntExpr(expr, &int_value));
      RETURN_NOT_OK(ConvertFromInt(result, int_value));
      break;
    }

    case InternalType::kFloatValue: FALLTHROUGH_INTENDED;
    case InternalType::kDoubleValue: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalDoubleExpr(expr, &double_value));
      RETURN_NOT_OK(ConvertFromDouble(result, double_value));
      break;
    }

    case InternalType::kStringValue: {
      EvalStringValue string_value;
      RETURN_NOT_OK(EvalStringExpr(expr, &string_value));
      RETURN_NOT_OK(ConvertFromString(result, string_value));
      break;
    }

    case InternalType::kBoolValue: {
      EvalBoolValue bool_value;
      RETURN_NOT_OK(EvalBoolExpr(expr, &bool_value));
      RETURN_NOT_OK(ConvertFromBool(result, bool_value));
      break;
    }

    case InternalType::kTimestampValue: {
      RETURN_NOT_OK(EvalTimestampExpr(expr, static_cast<EvalTimestampValue*>(result)));
      break;
    }

    case InternalType::kInetaddressValue: {
      RETURN_NOT_OK(EvalInetaddressExpr(expr, static_cast<EvalInetaddressValue*>(result)));
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

    case ExprOperator::kUMinus:
      result->value_ = -static_cast<const PTConstInt*>(e->op1().get())->Eval();
      break;

    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      switch (var->sql_type()) {
        case DataType::INT8:
          result->value_ = value.int8_value();
          break;
        case DataType::INT16:
          result->value_ = value.int16_value();
          break;
        case DataType::INT32:
          result->value_ = value.int32_value();
          break;
        case DataType::INT64:
          result->value_ = value.int64_value();
          break;
        default:
          LOG(FATAL) << "Unexpected integer type " << var->sql_type();
      }
      break;
    }

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

    case ExprOperator::kUMinus:
      result->value_ = -static_cast<const PTConstDouble*>(e->op1().get())->Eval();
      break;

    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      switch (var->sql_type()) {
        case DataType::FLOAT:
          result->value_ = value.float_value();
          break;
        case DataType::DOUBLE:
          result->value_ = value.double_value();
          break;
        default:
          LOG(FATAL) << "Unexpected floating point type " << var->sql_type();
      }
      break;
    }

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

    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      const string& string_value = value.string_value();
      result->value_ = MCString::MakeShared(exec_context_->PTempMem(),
                                            string_value.data(),
                                            string_value.length());
      break;
    }

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

    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      result->value_ = value.bool_value();
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::EvalTimestampExpr(const PTExpr::SharedPtr& expr, EvalTimestampValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      result->value_ = value.timestamp_value().ToInt64();
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::EvalInetaddressExpr(const PTExpr::SharedPtr& expr, EvalInetaddressValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      result->value_ = value.inetaddress_value();
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::ConvertFromInt(EvalValue *result, const EvalIntValue& int_value) {
  switch (result->datatype()) {
    case InternalType::kInt64Value:
      static_cast<EvalIntValue *>(result)->value_ = int_value.value_;
      break;

    case InternalType::kDoubleValue:
      static_cast<EvalDoubleValue *>(result)->value_ = int_value.value_;
      break;

    case InternalType::kTimestampValue: {
      int64_t val = int_value.value_;
      int64_t ts = DateTime::TimestampFromInt(val).ToInt64();
      static_cast<EvalTimestampValue *>(result)->value_ = ts;
      break;
    }

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

Status Executor::ConvertFromDouble(EvalValue *result, const EvalDoubleValue& double_value) {
  switch (result->datatype()) {
    case InternalType::kDoubleValue:
      static_cast<EvalDoubleValue *>(result)->value_ = double_value.value_;
      break;

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

Status Executor::ConvertFromString(EvalValue *result, const EvalStringValue& string_value) {
  switch (result->datatype()) {
    case InternalType::kStringValue:
      static_cast<EvalStringValue *>(result)->value_ = string_value.value_;
      break;

    case InternalType::kTimestampValue: {
      std::string s = string_value.value_.get()->c_str();
      Timestamp ts;
      RETURN_NOT_OK(DateTime::TimestampFromString(s, &ts));
      static_cast<EvalTimestampValue *>(result)->value_ = ts.ToInt64();
      break;
    }

    case InternalType::kInetaddressValue: {
      std::string s = string_value.value_.get()->c_str();
      InetAddress addr;
      RETURN_NOT_OK(addr.FromString(s));
      static_cast<EvalInetaddressValue*>(result)->value_ = addr;
      break;
    }

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

Status Executor::ConvertFromBool(EvalValue *result, const EvalBoolValue& bool_value) {
  switch (result->datatype()) {
    case InternalType::kBoolValue:
      static_cast<EvalBoolValue *>(result)->value_ = bool_value.value_;
      break;

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
