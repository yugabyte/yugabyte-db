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

#include "yb/bfql/bfql.h"

#include "yb/util/status_format.h"

#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"

namespace yb {
namespace ql {

using yb::bfql::BFOpcode;
using yb::bfql::BFOPCODE_NOOP;

Status Executor::PTExprToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb) {
  if (!bcall_pt->is_server_operator()) {
    // Regular builtin function call.
    return BFCallToPB(bcall_pt, expr_pb);
  } else {
    // Server builtin function call.
    return TSCallToPB(bcall_pt, expr_pb);
  }
}

Status Executor::BFCallToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb) {
  if (bcall_pt->result_cast_op() != BFOPCODE_NOOP) {
      QLBCallPB *cast_pb = expr_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(bcall_pt->result_cast_op()));

      // Result of the bcall_pt is the input of this CAST.
      expr_pb = cast_pb->add_operands();
  }

  QLBCallPB *bcall_pb = expr_pb->mutable_bfcall();
  bcall_pb->set_opcode(bcall_pt->bfopcode());

  int pindex = 0;
  const MCVector<yb::bfql::BFOpcode>& cast_ops = bcall_pt->cast_ops();
  const MCList<PTExpr::SharedPtr>& args = bcall_pt->args();

  for (const PTExpr::SharedPtr& arg : args) {
    // Create PB for the argument "arg".
    QLExpressionPB *operand_pb = bcall_pb->add_operands();

    if (cast_ops[pindex] != BFOPCODE_NOOP) {
      // Apply the cast operator. The return value of CAST is the operand of the actual BCALL.
      QLBCallPB *cast_pb = operand_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(cast_ops[pindex]));

      // Result of the argument, operand_pb, is the input of CAST.
      operand_pb = cast_pb->add_operands();
    }
    pindex++;

    // Process the argument and save the result to "operand_pb".
    RETURN_NOT_OK(PTExprToPB(arg, operand_pb));

    if (strcmp(bcall_pt->name()->c_str(), bfql::kCqlCastFuncName) == 0) {
      // For cql_cast, we just need one parameter.
      break;
    }
  }

  return Status::OK();
}

Status Executor::TSCallToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb) {
  if (bcall_pt->result_cast_op() != BFOPCODE_NOOP) {
      QLBCallPB *cast_pb = expr_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(bcall_pt->result_cast_op()));

      // Result of the bcall_pt is the input of this CAST.
      expr_pb = cast_pb->add_operands();
  }
  QLBCallPB *bcall_pb = expr_pb->mutable_tscall();
  bcall_pb->set_opcode(bcall_pt->bfopcode());
  int pindex = 0;
  const MCVector<yb::bfql::BFOpcode>& cast_ops = bcall_pt->cast_ops();
  const MCList<PTExpr::SharedPtr>& args = bcall_pt->args();

  for (const PTExpr::SharedPtr& arg : args) {
    // Create PB for the argument "arg".
    QLExpressionPB *operand_pb = bcall_pb->add_operands();

    if (cast_ops[pindex] != BFOPCODE_NOOP) {
      // Apply the cast operator. The return value of CAST is the operand of the actual BCALL.
      QLBCallPB *cast_pb = operand_pb->mutable_bfcall();
      cast_pb->set_opcode(static_cast<int32_t>(cast_ops[pindex]));

      // Result of the argument, operand_pb, is the input of CAST.
      operand_pb = cast_pb->add_operands();
    }
    pindex++;

    // Process the argument and save the result to "operand_pb".
    RETURN_NOT_OK(PTExprToPB(arg, operand_pb));
  }

  return Status::OK();
}

// Forming constructor call for collection and user-defined types.
Status Executor::PTExprToPB(const PTCollectionExpr *expr, QLExpressionPB *expr_pb) {
  bool is_frozen = false;
  DataType data_type = expr->ql_type()->main();
  if (data_type == DataType::FROZEN) {
    is_frozen = true;
    // Use the nested collection type.
    data_type = expr->ql_type()->param_type(0)->main();
  }

  QLExpressionPB *arg_pb;
  QLBCallPB *bcall_pb = expr_pb->mutable_bfcall();
  switch (data_type) {
    case DataType::MAP:
      if (is_frozen) {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_MAP_FROZEN));
      } else {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_MAP_CONSTRUCTOR));
      }
      RSTATUS_DCHECK_EQ(expr->keys().size(), expr->values().size(),
                        InternalError, "Invalid MAP literal");
      for (auto key_it = expr->keys().begin(), value_it = expr->values().begin();
           key_it != expr->keys().end() && value_it != expr->values().end();
           key_it++, value_it++) {
        arg_pb = bcall_pb->add_operands();
        RETURN_NOT_OK(PTExprToPB(*key_it, arg_pb));

        arg_pb = bcall_pb->add_operands();
        RETURN_NOT_OK(PTExprToPB(*value_it, arg_pb));
      }
      break;

    case DataType::SET:
      if (is_frozen) {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_SET_FROZEN));
      } else {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_SET_CONSTRUCTOR));
      }
      for (auto &elem : expr->values()) {
        arg_pb = bcall_pb->add_operands();
        RETURN_NOT_OK(PTExprToPB(elem, arg_pb));
      }
      break;

    case DataType::LIST:
      if (is_frozen) {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_LIST_FROZEN));
      } else {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_LIST_CONSTRUCTOR));
      }
      for (auto &elem : expr->values()) {
        arg_pb = bcall_pb->add_operands();
        RETURN_NOT_OK(PTExprToPB(elem, arg_pb));
      }
      break;

    case DataType::USER_DEFINED_TYPE: {
      auto field_values = expr->udtype_field_values();
      if (is_frozen) {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_UDT_FROZEN));
        for (size_t i = 0; i < field_values.size(); i++) {
          // Add values for all attributes in frozen UDT, including NULL values.
          arg_pb = bcall_pb->add_operands();
          if (field_values[i] != nullptr) {
            RETURN_NOT_OK(PTExprToPB(field_values[i], arg_pb));
          }
        }

      } else {
        bcall_pb->set_opcode(static_cast<int32_t>(BFOpcode::OPCODE_UDT_CONSTRUCTOR));
        for (size_t i = 0; i < field_values.size(); i++) {
          // Add [key, value] pairs to attributes if the value is not NULL.
          if (field_values[i] != nullptr) {
            // Add key.
            arg_pb = bcall_pb->add_operands();
            arg_pb->mutable_value()->set_int16_value(narrow_cast<int16_t>(i));

            // Add value.
            arg_pb = bcall_pb->add_operands();
            RETURN_NOT_OK(PTExprToPB(field_values[i], arg_pb));
          }
        }
      }
      break;
    }

    default:
      return STATUS(InternalError, "Invalid enum value");
  }

  return Status::OK();
}

}  // namespace ql
}  // namespace yb
