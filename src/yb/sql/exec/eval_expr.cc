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

  std::unique_ptr<YQLValueWithPB> value(new YQLValueWithPB());
  RETURN_NOT_OK(GetBindVariable(bind_pt, value.get()));

  if (value->IsNull()) {
    expr_pb->set_allocated_value(value.release());
    return Status::OK();
  }

  switch (bind_pt->yql_type_id()) {
    case DataType::INT8: FALLTHROUGH_INTENDED;
    case DataType::INT16: FALLTHROUGH_INTENDED;
    case DataType::INT32: FALLTHROUGH_INTENDED;
    case DataType::INT64: FALLTHROUGH_INTENDED;
    case DataType::FLOAT: FALLTHROUGH_INTENDED;
    case DataType::DOUBLE: FALLTHROUGH_INTENDED;
    case DataType::BOOL: FALLTHROUGH_INTENDED;
    case DataType::STRING: FALLTHROUGH_INTENDED;
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::TIMESTAMP: FALLTHROUGH_INTENDED;
    case DataType::INET: FALLTHROUGH_INTENDED;
    case DataType::UUID: FALLTHROUGH_INTENDED;
    case DataType::TIMEUUID: FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::LIST:
      expr_pb->set_allocated_value(value.release());
      return Status::OK();

    case DataType::DECIMAL: {
      const string& dbind = value->decimal_value();
      util::Decimal d;
      RETURN_NOT_OK(d.DecodeFromSerializedBigDecimal(Slice(dbind.data(), dbind.size())));
      expr_pb->mutable_value()->set_decimal_value(d.EncodeToComparable());
      return Status::OK();
    }

    case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case DataType::VARINT: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA:
      break;

    // default: fall through below
  }

  return STATUS_SUBSTITUTE(InternalError, "Unexpected bind type $0", bind_pt->yql_type_id());
}

CHECKED_STATUS Executor::GetBindVariable(const PTBindVar* var, YQLValue *value) const {
  return params_->GetBindVariable(string(var->name()->c_str()),
                                  var->pos(),
                                  var->yql_type(),
                                  value);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::PTExprToPB(const PTRef *ref_pt, YQLExpressionPB *ref_pb) {
  const ColumnDesc *col_desc = ref_pt->desc();
  ref_pb->set_column_id(col_desc->id());
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
