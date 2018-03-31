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

#include <string>

#include "yb/yql/pgsql/pbgen/pg_coder.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace pgsql {

using yb::bfpg::BFOPCODE_NOOP;

CHECKED_STATUS PgCoder::TExprToPB(const PgTBcall *bcall_pt, PgsqlExpressionPB *expr_pb) {
  if (!bcall_pt->is_server_operator()) {
    // Regular builtin function call.
    return BFCallToPB(bcall_pt, expr_pb);
  } else {
    // Server builtin function call.
    return TSCallToPB(bcall_pt, expr_pb);
  }
}

CHECKED_STATUS PgCoder::BFCallToPB(const PgTBcall *bcall_pt, PgsqlExpressionPB *expr_pb) {
  if (bcall_pt->result_cast_op() != BFOPCODE_NOOP) {
      PgsqlBCallPB *cast_pb = expr_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(bcall_pt->result_cast_op()));

      // Result of the bcall_pt is the input of this CAST.
      expr_pb = cast_pb->add_operands();
  }

  PgsqlBCallPB *bcall_pb = expr_pb->mutable_bfcall();
  bcall_pb->set_opcode(bcall_pt->bfopcode());

  int pindex = 0;
  const MCVector<yb::bfpg::BFOpcode>& cast_ops = bcall_pt->cast_ops();
  const MCList<PgTExpr::SharedPtr>& args = bcall_pt->args();

  for (const PgTExpr::SharedPtr& arg : args) {
    // Create PB for the argument "arg".
    PgsqlExpressionPB *operand_pb = bcall_pb->add_operands();

    if (cast_ops[pindex] != BFOPCODE_NOOP) {
      // Apply the cast operator. The return value of CAST is the operand of the actual BCALL.
      PgsqlBCallPB *cast_pb = operand_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(cast_ops[pindex]));

      // Result of the argument, operand_pb, is the input of CAST.
      operand_pb = cast_pb->add_operands();
    }
    pindex++;

    // Process the argument and save the result to "operand_pb".
    RETURN_NOT_OK(TExprToPB(arg, operand_pb));
  }

  return Status::OK();
}

CHECKED_STATUS PgCoder::TSCallToPB(const PgTBcall *bcall_pt, PgsqlExpressionPB *expr_pb) {
  if (bcall_pt->result_cast_op() != BFOPCODE_NOOP) {
      PgsqlBCallPB *cast_pb = expr_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(bcall_pt->result_cast_op()));

      // Result of the bcall_pt is the input of this CAST.
      expr_pb = cast_pb->add_operands();
  }
  PgsqlBCallPB *bcall_pb = expr_pb->mutable_tscall();
  bcall_pb->set_opcode(bcall_pt->bfopcode());
  int pindex = 0;
  const MCVector<yb::bfpg::BFOpcode>& cast_ops = bcall_pt->cast_ops();
  const MCList<PgTExpr::SharedPtr>& args = bcall_pt->args();

  for (const PgTExpr::SharedPtr& arg : args) {
    // Create PB for the argument "arg".
    PgsqlExpressionPB *operand_pb = bcall_pb->add_operands();

    if (cast_ops[pindex] != BFOPCODE_NOOP) {
      // Apply the cast operator. The return value of CAST is the operand of the actual BCALL.
      PgsqlBCallPB *cast_pb = operand_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(cast_ops[pindex]));

      // Result of the argument, operand_pb, is the input of CAST.
      operand_pb = cast_pb->add_operands();
    }
    pindex++;

    // Process the argument and save the result to "operand_pb".
    RETURN_NOT_OK(TExprToPB(arg, operand_pb));
  }

  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
