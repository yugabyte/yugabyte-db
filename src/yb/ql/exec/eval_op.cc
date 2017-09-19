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

}  // namespace ql
}  // namespace yb
