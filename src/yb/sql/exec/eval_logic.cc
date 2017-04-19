//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"

namespace yb {
namespace sql {

CHECKED_STATUS Executor::PTExprToPB(const PTLogic1 *logic_pt, YQLExpressionPB *logic_pb) {
  YQLConditionPB *cond = logic_pb->mutable_condition();
  cond->set_op(logic_pt->yql_op());

  RETURN_NOT_OK(PTExprToPB(logic_pt->op1(), cond->add_operands()));
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTLogic2 *logic_pt, YQLExpressionPB *logic_pb) {
  YQLConditionPB *cond = logic_pb->mutable_condition();
  cond->set_op(logic_pt->yql_op());

  RETURN_NOT_OK(PTExprToPB(logic_pt->op1(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(logic_pt->op2(), cond->add_operands()));
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTRelation0 *relation_pt, YQLExpressionPB *relation_pb) {
  YQLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->yql_op());
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTRelation1 *relation_pt, YQLExpressionPB *relation_pb) {
  YQLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->yql_op());

  RETURN_NOT_OK(PTExprToPB(relation_pt->op1(), cond->add_operands()));
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTRelation2 *relation_pt, YQLExpressionPB *relation_pb) {
  YQLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->yql_op());

  RETURN_NOT_OK(PTExprToPB(relation_pt->op1(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(relation_pt->op2(), cond->add_operands()));
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTRelation3 *relation_pt, YQLExpressionPB *relation_pb) {
  YQLConditionPB *cond = relation_pb->mutable_condition();
  cond->set_op(relation_pt->yql_op());

  RETURN_NOT_OK(PTExprToPB(relation_pt->op1(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(relation_pt->op2(), cond->add_operands()));
  RETURN_NOT_OK(PTExprToPB(relation_pt->op3(), cond->add_operands()));
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
