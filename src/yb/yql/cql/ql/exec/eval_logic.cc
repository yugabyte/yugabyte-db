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

Status Executor::PTExprToPB(const PTLogic1 *logic_pt, QLExpressionPB *logic_pb) {
  QLConditionPB *cond = logic_pb->mutable_condition();
  cond->set_op(logic_pt->ql_op());

  RETURN_NOT_OK(PTExprToPB(logic_pt->op1(), cond->add_operands()));
  return Status::OK();
}

Status Executor::PTExprToPB(const PTLogic2 *logic_pt, QLExpressionPB *logic_pb) {
  QLConditionPB *cond = logic_pb->mutable_condition();
  cond->set_op(logic_pt->ql_op());

  RETURN_NOT_OK(PTExprToPB(logic_pt->op1(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(logic_pt->op2(), cond->add_operands()));
  return Status::OK();
}

Status Executor::PTExprToPB(const PTRelation0 *relation_pt, QLExpressionPB *relation_pb) {
  QLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->ql_op());
  return Status::OK();
}

Status Executor::PTExprToPB(const PTRelation1 *relation_pt, QLExpressionPB *relation_pb) {
  QLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->ql_op());

  RETURN_NOT_OK(PTExprToPB(relation_pt->op1(), cond->add_operands()));
  return Status::OK();
}

Status Executor::PTExprToPB(const PTRelation2 *relation_pt, QLExpressionPB *relation_pb) {
  QLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->ql_op());

  RETURN_NOT_OK(PTExprToPB(relation_pt->op1(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(relation_pt->op2(), cond->add_operands()));
  return Status::OK();
}

Status Executor::PTExprToPB(const PTRelation3 *relation_pt, QLExpressionPB *relation_pb) {
  QLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->ql_op());

  RETURN_NOT_OK(PTExprToPB(relation_pt->op1(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(relation_pt->op2(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(relation_pt->op3(), cond->add_operands()));
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
