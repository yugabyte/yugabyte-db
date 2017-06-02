//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"

namespace yb {
namespace sql {

CHECKED_STATUS Executor::PTExprToPB(const PTBcall *bcall_pt, YQLExpressionPB *expr_pb) {
  if (bcall_pt->result_cast_op() != yb::bfyql::OPCODE_NOOP) {
      YQLBFCallPB *cast_pb = expr_pb->mutable_bfcall();
      cast_pb->set_bfopcode(static_cast<int32_t>(bcall_pt->result_cast_op()));

      // Result of the bcall_pt is the input of this CAST.
      expr_pb = cast_pb->add_operands();
  }

  YQLBFCallPB *bcall_pb = expr_pb->mutable_bfcall();
  bcall_pb->set_bfopcode(static_cast<int32_t>(bcall_pt->bf_opcode()));

  int pindex = 0;
  const MCVector<yb::bfyql::BFOpcode>& cast_ops = bcall_pt->cast_ops();
  const MCList<PTExpr::SharedPtr>& args = bcall_pt->args();

  for (const PTExpr::SharedPtr& arg : args) {
    // Create PB for the argument "arg".
    YQLExpressionPB *operand_pb = bcall_pb->add_operands();

    if (cast_ops[pindex] != yb::bfyql::OPCODE_NOOP) {
      // Apply the cast operator. The return value of CAST is the operand of the actual BCALL.
      YQLBFCallPB *cast_pb = operand_pb->mutable_bfcall();
      cast_pb->set_bfopcode(static_cast<int32_t>(cast_ops[pindex]));

      // Result of the argument, operand_pb, is the input of CAST.
      operand_pb = cast_pb->add_operands();
    }
    pindex++;

    // Process the argument and save the result to "operand_pb".
    RETURN_NOT_OK(PTExprToPB(arg, operand_pb));
  }

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
