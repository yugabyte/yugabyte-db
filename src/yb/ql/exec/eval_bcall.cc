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

#include "yb/ql/exec/executor.h"

namespace yb {
namespace ql {

CHECKED_STATUS Executor::PTExprToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb) {
  if (bcall_pt->result_cast_op() != yb::bfql::OPCODE_NOOP) {
      QLBFCallPB *cast_pb = expr_pb->mutable_bfcall();
      cast_pb->set_bfopcode(static_cast<int32_t>(bcall_pt->result_cast_op()));

      // Result of the bcall_pt is the input of this CAST.
      expr_pb = cast_pb->add_operands();
  }

  QLBFCallPB *bcall_pb = expr_pb->mutable_bfcall();
  bcall_pb->set_bfopcode(static_cast<int32_t>(bcall_pt->bf_opcode()));

  int pindex = 0;
  const MCVector<yb::bfql::BFOpcode>& cast_ops = bcall_pt->cast_ops();
  const MCList<PTExpr::SharedPtr>& args = bcall_pt->args();

  for (const PTExpr::SharedPtr& arg : args) {
    // Create PB for the argument "arg".
    QLExpressionPB *operand_pb = bcall_pb->add_operands();

    if (cast_ops[pindex] != yb::bfql::OPCODE_NOOP) {
      // Apply the cast operator. The return value of CAST is the operand of the actual BCALL.
      QLBFCallPB *cast_pb = operand_pb->mutable_bfcall();
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

}  // namespace ql
}  // namespace yb
