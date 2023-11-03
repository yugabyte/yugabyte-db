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

#include "yb/yql/cql/ql/ptree/pt_expr.h"

namespace yb {
namespace ql {

Status Executor::PTUMinusToPB(const PTOperator1 *op_pt, QLExpressionPB *op_pb) {
  return PTUMinusToPB(op_pt, op_pb->mutable_value());
}

Status Executor::PTUMinusToPB(const PTOperator1 *op_pt, QLValuePB *const_pb) {
  // Negate the value.
  if (op_pt->is_constant()) {
    RETURN_NOT_OK(PTConstToPB(op_pt->op1(), const_pb, true));
  } else {
    LOG(FATAL) << "Unary minus operator is only supported for literals";
  }

  return Status::OK();
}

Status Executor::PTJsonOperatorToPB(const PTJsonOperator::SharedPtr& json_pt,
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

}  // namespace ql
}  // namespace yb
