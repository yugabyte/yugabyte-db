//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/docdb/doc_expr.h"

#include "yb/client/schema.h"

#include "yb/docdb/subdocument.h"

namespace yb {
namespace docdb {

using yb::bfql::TSOpcode;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS DocExprExecutor::EvalTSCall(const QLBCallPB& tscall,
                                           const QLTableRow& table_row,
                                           QLValue *result) {
  TSOpcode tsopcode = static_cast<TSOpcode>(tscall.opcode());
  switch (tsopcode) {
    case TSOpcode::kNoOp:
    case TSOpcode::kScalarInsert:
      LOG(FATAL) << "Client should not generate function call instruction with operator "
                 << static_cast<int>(tsopcode);
      break;

    case TSOpcode::kTtl: {
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      int64_t ttl_seconds = -1;
      RETURN_NOT_OK(table_row.GetTTL(tscall.operands(0).column_id(), &ttl_seconds));
      if (ttl_seconds != -1) {
        result->set_int64_value(ttl_seconds);
      } else {
        result->SetNull();
      }
      return Status::OK();
    }

    case TSOpcode::kWriteTime: {
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      int64_t write_time = 0;
      RETURN_NOT_OK(table_row.GetWriteTime(tscall.operands(0).column_id(), &write_time));
      result->set_int64_value(write_time);
      return Status::OK();
    }

    case TSOpcode::kCount:
      if (tscall.operands(0).has_column_id()) {
        // Check if column value is NULL. CQL does not count NULL value of a column.
        QLValue arg_result;
        RETURN_NOT_OK(EvalExpr(tscall.operands(0), table_row, &arg_result));
        if (arg_result.IsNull()) {
          return Status::OK();
        }
      }
      return EvalCount(result);

    case TSOpcode::kSum: {
      QLValue arg_result;
      RETURN_NOT_OK(EvalExpr(tscall.operands(0), table_row, &arg_result));
      return EvalSum(arg_result, result);
    }

    case TSOpcode::kMin: {
      QLValue arg_result;
      RETURN_NOT_OK(EvalExpr(tscall.operands(0), table_row, &arg_result));
      return EvalMin(arg_result, result);
    }

    case TSOpcode::kMax: {
      QLValue arg_result;
      RETURN_NOT_OK(EvalExpr(tscall.operands(0), table_row, &arg_result));
      return EvalMax(arg_result, result);
    }

    case TSOpcode::kAvg:
      return STATUS(RuntimeError, "Not yet supported");

    case TSOpcode::kMapExtend: FALLTHROUGH_INTENDED;
    case TSOpcode::kMapRemove: FALLTHROUGH_INTENDED;
    case TSOpcode::kSetExtend: FALLTHROUGH_INTENDED;
    case TSOpcode::kSetRemove: FALLTHROUGH_INTENDED;
    case TSOpcode::kListAppend:
      // Return the value of the second operand. The first operand must be a column ID.
      return EvalExpr(tscall.operands(1), table_row, result);

    case TSOpcode::kListPrepend:
      // Return the value of the second operand. The first operand must be a column ID.
      return EvalExpr(tscall.operands(0), table_row, result);

    case TSOpcode::kListRemove: {
      QLValue org_list_value;
      QLValue sub_list_value;
      RETURN_NOT_OK(EvalExpr(tscall.operands(0), table_row, &org_list_value));
      RETURN_NOT_OK(EvalExpr(tscall.operands(1), table_row, &sub_list_value));

      result->set_list_value();
      if (!org_list_value.IsNull() && !sub_list_value.IsNull()) {
        const QLSeqValuePB& org_list = org_list_value.list_value();
        const QLSeqValuePB& sub_list = sub_list_value.list_value();
        for (const QLValuePB& org_elem : org_list.elems()) {
          bool should_remove = false;
          for (const QLValuePB& sub_elem : sub_list.elems()) {
            if (org_elem == sub_elem) {
              should_remove = true;
              break;
            }
          }
          if (!should_remove) {
            auto elem = result->add_list_elem();
            elem->CopyFrom(org_elem);
          }
        }
      }
      return Status::OK();
    }
  }

  result->SetNull();
  return Status::OK();
}

CHECKED_STATUS DocExprExecutor::EvalCount(QLValue *aggr_count) {
  if (aggr_count->IsNull()) {
    aggr_count->set_int64_value(1);
  } else {
    aggr_count->set_int64_value(aggr_count->int64_value() + 1);
  }
  return Status::OK();
}

CHECKED_STATUS DocExprExecutor::EvalSum(const QLValue& val, QLValue *aggr_sum) {
  if (val.IsNull()) {
    return Status::OK();
  }

  if (aggr_sum->IsNull()) {
    *aggr_sum = val;
    return Status::OK();
  }

  switch (aggr_sum->type()) {
    case InternalType::kInt8Value:
      aggr_sum->set_int8_value(aggr_sum->int8_value() + val.int8_value());
      break;
    case InternalType::kInt16Value:
      aggr_sum->set_int16_value(aggr_sum->int16_value() + val.int16_value());
      break;
    case InternalType::kInt32Value:
      aggr_sum->set_int32_value(aggr_sum->int32_value() + val.int32_value());
      break;
    case InternalType::kInt64Value:
      aggr_sum->set_int64_value(aggr_sum->int64_value() + val.int64_value());
      break;
    case InternalType::kFloatValue:
      aggr_sum->set_float_value(aggr_sum->float_value() + val.float_value());
      break;
    case InternalType::kDoubleValue:
      aggr_sum->set_double_value(aggr_sum->double_value() + val.double_value());
      break;
    default:
      return STATUS(RuntimeError, "Cannot find SUM of this column");
  }
  return Status::OK();
}

CHECKED_STATUS DocExprExecutor::EvalMax(const QLValue& val, QLValue *aggr_max) {
  if (!val.IsNull() && (aggr_max->IsNull() || *aggr_max < val)) {
    *aggr_max = val;
  }
  return Status::OK();
}

CHECKED_STATUS DocExprExecutor::EvalMin(const QLValue& val, QLValue *aggr_min) {
  if (!val.IsNull() && (aggr_min->IsNull() || *aggr_min > val)) {
    *aggr_min = val;
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
