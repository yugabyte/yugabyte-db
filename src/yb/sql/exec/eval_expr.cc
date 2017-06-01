//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <string>

#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/util/date_time.h"
#include "yb/util/net/inetaddress.h"

DECLARE_bool(yql_experiment_support_expression);

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

Status Executor::PTExprToPB(const PTExpr::SharedPtr& expr, YQLExpressionPB *expr_pb) {
  switch (expr->expr_op()) {
    case ExprOperator::kNoOp:
      return Status::OK();

    case ExprOperator::kConst:
    case ExprOperator::kCollection: {
      YQLValuePB *const_pb = expr_pb->mutable_value();
      return PTConstToPB(expr, const_pb);
    }

    case ExprOperator::kRef:
      return PTExprToPB(static_cast<const PTRef*>(expr.get()), expr_pb);

    case ExprOperator::kBindVar:
      return PTExprToPB(static_cast<const PTBindVar*>(expr.get()), expr_pb);

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
      LOG(FATAL) << "Not supported operator";
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::PTExprToPB(const PTBindVar *bind_pt, YQLExpressionPB *expr_pb) {
  // TODO(neil) This error should be raised by CQL when it compares between bind variables and
  // bind arguments before calling YQL layer to execute.
  if (params_ == nullptr) {
    return STATUS(RuntimeError, "no bind variable supplied");
  }

  YQLValueWithPB value;
  YQLValuePB *bind_pb = expr_pb->mutable_value();

  // TODO (mihnea or robert) GetBindVariable should take "YQLValuePB" as input, so we don't have to
  // copy from bind to YQLValue and then to YQLValuePB.
  RETURN_NOT_OK(GetBindVariable(bind_pt, &value));
  if (value.IsNull()) {
    return Status::OK();
  }

  switch (bind_pt->yql_type_id()) {
    case DataType::INT8:
      bind_pb->set_int8_value(value.int8_value());
      break;
    case DataType::INT16:
      bind_pb->set_int16_value(value.int16_value());
      break;
    case DataType::INT32:
      bind_pb->set_int32_value(value.int32_value());
      break;
    case DataType::INT64:
      bind_pb->set_int64_value(value.int64_value());
      break;
    case DataType::FLOAT:
      bind_pb->set_float_value(value.float_value());
      break;
    case DataType::DOUBLE:
      bind_pb->set_double_value(value.double_value());
      break;
    case DataType::DECIMAL: {
      const string& dbind = value.decimal_value();
      util::Decimal d;
      RETURN_NOT_OK(d.DecodeFromSerializedBigDecimal(Slice(dbind.data(), dbind.size())));
      bind_pb->set_decimal_value(d.EncodeToComparable());
      break;
    }
    case DataType::BOOL:
      bind_pb->set_bool_value(value.bool_value());
      break;
    case DataType::STRING:
      bind_pb->set_string_value(value.string_value());
      break;
    case DataType::BINARY:
      bind_pb->set_binary_value(value.binary_value());
      break;
    case DataType::TIMESTAMP:
      bind_pb->set_timestamp_value(value.timestamp_value().ToInt64());
      break;
    case DataType::INET:
      YQLValue::set_inetaddress_value(value.inetaddress_value(), bind_pb);
      break;
    case DataType::UUID:
      YQLValue::set_uuid_value(value.uuid_value(), bind_pb);
      break;
    case DataType::TIMEUUID:
      YQLValue::set_timeuuid_value(value.timeuuid_value(), bind_pb);
      break;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::LIST:
      // TODO (mihnea) refactor YQLValue to avoid copying here and below
      bind_pb->CopyFrom(value.value());
      break;
    default:
      LOG(FATAL) << "Unexpected integer type " << bind_pt->yql_type_id();
  }
  return Status::OK();
}

CHECKED_STATUS Executor::GetBindVariable(const PTBindVar* var, YQLValue *value) const {
  if (var->name() == nullptr) {
    return params_->GetBindVariable(nullptr, var->pos(), var->yql_type(), value);
  } else {
    string name(var->name()->c_str());
    return params_->GetBindVariable(&name, var->pos(), var->yql_type(), value);
  }
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::PTExprToPB(const PTRef *ref_pt, YQLExpressionPB *ref_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  ref_pb->set_column_id(col_desc->id());
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
