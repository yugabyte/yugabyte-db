//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <string>

#include "yb/sql/exec/eval_expr.h"
#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/util/date_time.h"

DECLARE_bool(yql_experiment_support_expression);

namespace yb {
namespace sql {

EvalValue::~EvalValue() {}

Status Executor::GetBindVariable(const PTBindVar* var, YQLValue *value) const {
  return params_->GetBindVariable(string(var->name()->c_str()),
                                  var->pos(),
                                  var->yql_type(),
                                  value);
}

Status Executor::EvalExpr(const PTExpr::SharedPtr& expr, EvalValue *result) {

  switch (expr->internal_type()) {
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

    case InternalType::kVarintStringValue: {
      EvalVarIntStringValue varint_value;
      RETURN_NOT_OK(EvalVarIntExpr(expr, &varint_value));
      RETURN_NOT_OK(ConvertFromVarInt(result, varint_value));
      break;
    }

    case InternalType::kFloatValue: FALLTHROUGH_INTENDED;
    case InternalType::kDoubleValue: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalDoubleExpr(expr, &double_value));
      RETURN_NOT_OK(ConvertFromDouble(result, double_value));
      break;
    }

    case InternalType::kDecimalValue: {
      EvalDecimalValue decimal_value;
      RETURN_NOT_OK(EvalDecimalExpr(expr, &decimal_value));
      RETURN_NOT_OK(ConvertFromDecimal(result, decimal_value));
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

    case InternalType::kBinaryValue: {
      EvalBinaryValue binary_value;
      RETURN_NOT_OK(EvalBinaryExpr(expr, &binary_value));
      RETURN_NOT_OK(ConvertFromBinary(result, binary_value));
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

    case InternalType::kUuidValue: {
      RETURN_NOT_OK(EvalUuidExpr(expr, static_cast<EvalUuidValue*>(result)));
      break;
    }

    default:
      LOG(FATAL) << "Unknown expression datatype " << expr->internal_type();
  }

  return Status::OK();
}

Status Executor::EvalIntExpr(const PTExpr::SharedPtr& expr, EvalIntValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kBfunc:
      if (FLAGS_yql_experiment_support_expression) {
        e = static_cast<const PTBfunc*>(e)->result().get();
      } else {
        return exec_context_->Error(expr->loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      FALLTHROUGH_INTENDED;
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
      if (value.IsNull()) {
        result->set_null();
      } else {
        switch (var->yql_type_id()) {
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
            LOG(FATAL) << "Unexpected integer type " << var->yql_type_id();
        }
      }
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }
  return eval_status;
}

Status Executor::EvalVarIntExpr(const PTExpr::SharedPtr& expr, EvalVarIntStringValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstVarInt*>(e)->Eval();
      break;

    case ExprOperator::kUMinus:
      result->value_ = static_cast<const PTConstVarInt*>(e->op1().get())->Eval();
      CHECK_GT(result->value_->size(), 0);
      // This method can be called more than twice. Ensure we don't keep prepending negative signs.
      if (*result->value_->data() != '-') {
        result->value_->insert(0, "-");
      }
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
    case ExprOperator::kBfunc:
      if (FLAGS_yql_experiment_support_expression) {
        e = static_cast<const PTBfunc*>(e)->result().get();
      } else {
        return exec_context_->Error(expr->loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      FALLTHROUGH_INTENDED;
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
      if (value.IsNull()) {
        result->set_null();
      } else {
        switch (var->yql_type_id()) {
          case DataType::FLOAT:
            result->value_ = value.float_value();
            break;
          case DataType::DOUBLE:
            result->value_ = value.double_value();
            break;
          default:
            LOG(FATAL) << "Unexpected floating point type " << var->yql_type_id();
        }
      }
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }
  return eval_status;
}

Status Executor::EvalDecimalExpr(const PTExpr::SharedPtr& expr, EvalDecimalValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {

    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstDecimal *>(e)->Eval();
      break;

    case ExprOperator::kUMinus:
      result->value_ = static_cast<const PTConstDecimal*>(e->op1().get())->Eval();
      CHECK_GT(result->value_->size(), 0);
      // This method can be called more than twice. Ensure we don't keep prepending negative signs.
      if (*result->value_->data() != '-') {
        result->value_->insert(0, "-");
      }
      break;

    default:
      LOG(FATAL) << "Operator not supported";
  }
  return eval_status;
}

Status Executor::EvalStringExpr(const PTExpr::SharedPtr& expr, EvalStringValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kBfunc:
      if (FLAGS_yql_experiment_support_expression) {
        e = static_cast<const PTBfunc*>(e)->result().get();
      } else {
        return exec_context_->Error(expr->loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      FALLTHROUGH_INTENDED;
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
      if (value.IsNull()) {
        result->set_null();
      } else {
        const string& string_value = value.string_value();
        result->value_ = MCString::MakeShared(exec_context_->PTempMem(),
                                              string_value.data(),
                                              string_value.length());
      }
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
      if (value.IsNull()) {
        result->set_null();
      } else {
        result->value_ = value.bool_value();
      }
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::EvalBinaryExpr(const PTExpr::SharedPtr& expr, EvalBinaryValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst:
      result->value_ = static_cast<const PTConstBinary*>(e)->Eval();
      break;

    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      if (value.IsNull()) {
        result->set_null();
      } else {
        // TODO(mihnea) this conversion shouldn't be needed, but needs a refactor to handle properly
        string literal_value = b2a_hex(value.binary_value());
        result->value_ = MCString::MakeShared(exec_context_->PTempMem(),
                                              literal_value.data(),
                                              literal_value.size());
      }
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
      if (value.IsNull()) {
        result->set_null();
      } else {
        result->value_ = value.timestamp_value().ToInt64();
      }
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
      if (value.IsNull()) {
        result->set_null();
      } else {
        result->value_ = value.inetaddress_value();
      }
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }

  return eval_status;
}

Status Executor::EvalUuidExpr(const PTExpr::SharedPtr& expr, EvalUuidValue *result) {
  Status eval_status = Status::OK();

  const PTExpr *e = expr.get();
  switch (expr->expr_op()) {
    case ExprOperator::kConst: {
      Uuid uuid;
      MCString::SharedPtr str = static_cast<const PTConstUuid*>(e)->Eval();
      RETURN_NOT_OK(uuid.FromString(str->c_str()));
      result->value_ = uuid;
      break;
    }
    case ExprOperator::kBindVar: {
      if (params_ == nullptr) {
        return STATUS(RuntimeError, "no bind variable supplied");
      }
      const PTBindVar *var = static_cast<const PTBindVar*>(e);
      YQLValueWithPB value;
      RETURN_NOT_OK(GetBindVariable(var, &value));
      if (value.IsNull()) {
        result->set_null();
      } else {
        result->value_ = value.uuid_value();
      }
      break;
    }

    default:
      LOG(FATAL) << "Not supported operator";
  }
  return eval_status;
}

Status Executor::ConvertFromInt(EvalValue *result, const EvalIntValue& int_value) {
  if (int_value.is_null()) {
    result->set_null();
  } else {
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
  }
  return Status::OK();
}

Status Executor::ConvertFromVarInt(EvalValue *result, const EvalVarIntStringValue& varint_value) {
  if (varint_value.is_null()) {
    result->set_null();
  } else {
    switch (result->datatype()) {
      case InternalType::kVarintValue:
        LOG(FATAL) << "VARINT type not supported";

      case InternalType::kVarintStringValue:
        static_cast<EvalVarIntStringValue *>(result)->value_ = varint_value.value_;
        break;

      case InternalType::kInt64Value: {
        auto *value = &(static_cast<EvalIntValue *>(result)->value_);
        *value = std::stol(varint_value.value_->c_str());
        break;
      }

      case InternalType::kDoubleValue: {
        auto *value = &(static_cast<EvalDoubleValue *>(result)->value_);
        *value = std::stold(varint_value.value_->c_str());
        break;
      }

      case InternalType::kDecimalValue: {
        util::Decimal decimal;
        CHECK_OK(decimal.FromString(varint_value.value_->c_str()));
        static_cast<EvalDecimalValue *>(result)->value_ =
            MCString::MakeShared(varint_value.value_->mem_ctx(),
                                 decimal.EncodeToComparable().c_str());
        break;
      }

      case InternalType::kTimestampValue: {
        int64_t val = std::stol(varint_value.value_->c_str());
        int64_t ts = DateTime::TimestampFromInt(val).ToInt64();
        static_cast<EvalTimestampValue *>(result)->value_ = ts;
        break;
      }

      default:
        LOG(FATAL) << "Illegal datatype conversion";
    }
  }
  return Status::OK();
}

Status Executor::ConvertFromDouble(EvalValue *result, const EvalDoubleValue& double_value) {
  if (double_value.is_null()) {
    result->set_null();
  } else {
    switch (result->datatype()) {
      case InternalType::kDoubleValue:
        static_cast<EvalDoubleValue *>(result)->value_ = double_value.value_;
        break;

      default:
        LOG(FATAL) << "Illegal datatype conversion";
    }
  }
  return Status::OK();
}

Status Executor::ConvertFromString(EvalValue *result, const EvalStringValue& string_value) {
  if (string_value.is_null()) {
    result->set_null();
  } else {
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

      case InternalType::kUuidValue: {
        std::string s = string_value.value_.get()->c_str();
        Uuid uuid;
        RETURN_NOT_OK(uuid.FromString(s));
        static_cast<EvalUuidValue*>(result)->value_ = uuid;
        break;
      }
      default:
        LOG(FATAL) << "Illegal datatype conversion";
    }
  }
  return Status::OK();
}

Status Executor::ConvertFromDecimal(EvalValue *result, const EvalDecimalValue& decimal_value) {
  if (decimal_value.is_null()) {
    result->set_null();
  } else {
    switch (result->datatype()) {
      case InternalType::kDecimalValue: {
        util::Decimal decimal;
        CHECK_OK(decimal.FromString(decimal_value.value_->c_str()));
        static_cast<EvalDecimalValue *>(result)->value_ =
            MCString::MakeShared(decimal_value.value_->mem_ctx(),
                                 decimal.EncodeToComparable().c_str());

        auto s = decimal.EncodeToComparable();
        string r;
        for (int i = 0; i < s.size(); i++) {
          r += StringPrintf("\\x%02x", (unsigned char)s[i]);
        }

        decimal.Negate();
        s = decimal.EncodeToComparable();
        r = "";
        for (int i = 0; i < s.size(); i++) {
          r += StringPrintf("\\x%02x", (unsigned char)s[i]);
        }

        break;
      }

      case InternalType::kDoubleValue: {
        static_cast<EvalDoubleValue *>(result)->value_ = std::stold(decimal_value.value_->c_str());
        break;
      }

      default:
        LOG(FATAL) << "Illegal datatype conversion";
    }
  }
  return Status::OK();
}

Status Executor::ConvertFromBool(EvalValue *result, const EvalBoolValue& bool_value) {
  if (bool_value.is_null()) {
    result->set_null();
  } else {
    switch (result->datatype()) {
      case InternalType::kBoolValue:
        static_cast<EvalBoolValue *>(result)->value_ = bool_value.value_;
        break;

      default:
        LOG(FATAL) << "Illegal datatype conversion";
    }
  }
  return Status::OK();
}

Status Executor::ConvertFromBinary(EvalValue *result, const EvalBinaryValue& binary_value) {
  if (binary_value.is_null()) {
    result->set_null();
  } else {
    switch (result->datatype()) {
      case InternalType::kBinaryValue: {
        int input_size = static_cast<int>(binary_value.value_->size());
        if (input_size % 2 != 0) {
          return STATUS(RuntimeError, "Invalid binary input, expected even number of hex digits");
        }
        int no_bytes = input_size / 2; // one byte for every two digits
        string hex_value;
        a2b_hex(binary_value.value_->c_str(), &hex_value, no_bytes);
        static_cast<EvalBinaryValue *>(result)->value_ =
            MCString::MakeShared(binary_value.value_->mem_ctx(), hex_value.data(), no_bytes);
        break;
      }

      default:
        LOG(FATAL) << "Illegal datatype conversion";
    }
  }
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
