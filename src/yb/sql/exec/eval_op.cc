//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"

namespace yb {
namespace sql {

CHECKED_STATUS Executor::PTUMinusToPB(const PTOperator1 *op_pt, YQLExpressionPB *op_pb) {
  return PTUMinusToPB(op_pt, op_pb->mutable_value());
}

CHECKED_STATUS Executor::PTUMinusToPB(const PTOperator1 *op_pt, YQLValuePB *const_pb) {
  // Negate the value.
  if (op_pt->is_constant()) {
    RETURN_NOT_OK(PTConstToPB(op_pt->op1(), const_pb, true));
  } else {
    LOG(FATAL) << "Unary minus operator is only supported for literals";
  }

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
