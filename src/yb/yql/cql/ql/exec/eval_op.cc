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

#include "yb/yql/cql/ql/exec/executor.h"

namespace yb {
namespace ql {

using client::YBColumnSpec;

CHECKED_STATUS Executor::PTUMinusToPB(const PTOperator1 *op_pt, QLExpressionPB *op_pb) {
  return PTUMinusToPB(op_pt, op_pb->mutable_value());
}

CHECKED_STATUS Executor::PTUMinusToPB(const PTOperator1 *op_pt, QLValuePB *const_pb) {
  // Negate the value.
  if (op_pt->is_constant()) {
    RETURN_NOT_OK(PTConstToPB(op_pt->op1(), const_pb, true));
  } else {
    LOG(FATAL) << "Unary minus operator is only supported for literals";
  }

  return Status::OK();
}

CHECKED_STATUS Executor::PTJsonOperatorToPB(const PTJsonOperator::SharedPtr& json_pt,
                                            QLJsonOperationPB *op_pb) {
  switch (json_pt->json_operator()) {
    case JsonOperator::JSON_TEXT:
      op_pb->set_json_operator(JsonOperatorPB::JSON_TEXT);
      break;
    case JsonOperator::JSON_OBJECT:
      op_pb->set_json_operator(JsonOperatorPB::JSON_OBJECT);
      break;
  }
  return PTExprToPB(json_pt->arg(), op_pb->mutable_operand());
}

CHECKED_STATUS Executor::ColumnOpsToSchema(const PTColumnDefinition *col,
                                           YBColumnSpec *col_spec) {
  PTExprListNode::SharedPtr op_list = col->operators();

  if (!op_list) {
    return Status::OK();
  }

  for (const auto& tnode : op_list->node_list()) {
    auto json_op_node = down_cast<PTJsonOperator*>(tnode.get());
    auto json_op = json_op_node->json_operator();

    if (json_op != JsonOperator::JSON_OBJECT && json_op != JsonOperator::JSON_TEXT) {
      return exec_context_->Error(tnode, "Invalid operator", ErrorCode::CQL_STATEMENT_INVALID);
    }

    JsonOperatorPB json_op_pb = (json_op == JsonOperator::JSON_TEXT ? JSON_TEXT : JSON_OBJECT);
    const PTExpr::SharedPtr arg = json_op_node->arg();

    switch (arg->internal_type()) {
      case InternalType::kStringValue: {
        const PTConstText* const text = dynamic_cast<const PTConstText*>(arg.get());
        col_spec->JsonOp(json_op_pb, text->QLName());
      }
      break;

      case InternalType::kVarintValue: {
        const PTConstVarInt* const num = dynamic_cast<const PTConstVarInt*>(arg.get());
        col_spec->JsonOp(json_op_pb, num->value()->c_str());
      }
      break;

      default:
        return exec_context_->Error(arg,
            "Invalid operator argument type", ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  return Status::OK();
}

}  // namespace ql
}  // namespace yb
