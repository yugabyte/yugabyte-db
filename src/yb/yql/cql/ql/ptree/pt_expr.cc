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
//
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/util/decimal.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/stol_utils.h"

namespace yb {
namespace ql {

using client::YBColumnSchema;
using std::shared_ptr;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTExpr::CheckOperator(SemContext *sem_context) {
  // Where clause only allow AND, EQ, LT, LE, GT, and GE operators.
  if (sem_context->where_state() != nullptr) {
    switch (ql_op_) {
      case QL_OP_AND:
      case QL_OP_EQUAL:
      case QL_OP_LESS_THAN:
      case QL_OP_LESS_THAN_EQUAL:
      case QL_OP_GREATER_THAN:
      case QL_OP_GREATER_THAN_EQUAL:
      case QL_OP_IN:
      case QL_OP_NOT_IN:
      case QL_OP_NOOP:
        break;
      default:
        return sem_context->Error(this, "This operator is not allowed in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
    }
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context) {
  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1) {
  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2) {
  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2,
                                       PTExpr::SharedPtr op3) {
  return Status::OK();
}

CHECKED_STATUS PTExpr::SetupSemStateForOp1(SemState *sem_state) {
  return Status::OK();
}

CHECKED_STATUS PTExpr::SetupSemStateForOp2(SemState *sem_state) {
  // Passing down where clause state variables.
  return Status::OK();
}

CHECKED_STATUS PTExpr::SetupSemStateForOp3(SemState *sem_state) {
  return Status::OK();
}

CHECKED_STATUS PTExpr::CheckExpectedTypeCompatibility(SemContext *sem_context) {
  CHECK(has_valid_internal_type() && has_valid_ql_type_id());

  // Check if RHS support counter update.
  if (sem_context->processing_set_clause() &&
      sem_context->lhs_col() != nullptr &&
      sem_context->lhs_col()->is_counter()) {
    RETURN_NOT_OK(this->CheckCounterUpdateSupport(sem_context));
  }

  // Check if RHS is convertible to LHS.
  if (!sem_context->expr_expected_ql_type()->IsUnknown()) {
    if (!sem_context->IsConvertible(sem_context->expr_expected_ql_type(), ql_type_)) {
      return sem_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
    }
  }

  // Resolve internal type.
  const InternalType expected_itype = sem_context->expr_expected_internal_type();
  if (expected_itype == InternalType::VALUE_NOT_SET) {
    expected_internal_type_ = internal_type_;
  } else {
    expected_internal_type_ = expected_itype;
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
CHECKED_STATUS PTExpr::CheckInequalityOperands(SemContext *sem_context,
                                               PTExpr::SharedPtr lhs,
                                               PTExpr::SharedPtr rhs) {
  if (!sem_context->IsComparable(lhs->ql_type_id(), rhs->ql_type_id())) {
    return sem_context->Error(this, "Cannot compare values of these datatypes",
                              ErrorCode::INCOMPARABLE_DATATYPES);
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::CheckEqualityOperands(SemContext *sem_context,
                                             PTExpr::SharedPtr lhs,
                                             PTExpr::SharedPtr rhs) {
  if (QLType::IsNull(lhs->ql_type_id()) || QLType::IsNull(rhs->ql_type_id())) {
    return Status::OK();
  } else {
    return CheckInequalityOperands(sem_context, lhs, rhs);
  }
}


CHECKED_STATUS PTExpr::CheckLhsExpr(SemContext *sem_context) {
  if (op_ != ExprOperator::kRef && op_ != ExprOperator::kBcall) {
    return sem_context->Error(this,
                              "Only column refs and builtin calls are allowed for left hand value",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::CheckRhsExpr(SemContext *sem_context) {
  // Check for limitation in QL (Not all expressions are acceptable).
  switch (op_) {
    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kCollection: FALLTHROUGH_INTENDED;
    case ExprOperator::kUMinus: FALLTHROUGH_INTENDED;
    case ExprOperator::kBindVar: FALLTHROUGH_INTENDED;
    case ExprOperator::kRef: FALLTHROUGH_INTENDED;
    case ExprOperator::kBcall:
      break;
    default:
      return sem_context->Error(this, "Operator not allowed as right hand value",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::CheckCounterUpdateSupport(SemContext *sem_context) const {
  return sem_context->Error(this, ErrorCode::INVALID_COUNTING_EXPR);
}

//--------------------------------------------------------------------------------------------------

PTLiteralString::PTLiteralString(MCSharedPtr<MCString> value)
    : PTLiteral<MCSharedPtr<MCString>>(value) {
}

PTLiteralString::~PTLiteralString() {
}

CHECKED_STATUS PTLiteralString::ToInt64(int64_t *value, bool negate) const {
  auto temp = negate ? util::CheckedStoll(string("-") + value_->c_str())
                     : util::CheckedStoll(*value_);
  RETURN_NOT_OK(temp);
  *value = *temp;
  return Status::OK();
}

CHECKED_STATUS PTLiteralString::ToDouble(long double *value, bool negate) const {
  auto temp = util::CheckedStold(*value_);
  RETURN_NOT_OK(temp);
  *value = negate ? -*temp : *temp;
  return Status::OK();
}

CHECKED_STATUS PTLiteralString::ToDecimal(util::Decimal *value, bool negate) const {
  if (negate) {
    return value->FromString(string("-") + value_->c_str());
  } else {
    return value->FromString(value_->c_str());
  }
}

CHECKED_STATUS PTLiteralString::ToDecimal(string *value, bool negate) const {
  util::Decimal d;
  if (negate) {
    RETURN_NOT_OK(d.FromString(string("-") + value_->c_str()));
  } else {
    RETURN_NOT_OK(d.FromString(value_->c_str()));
  }
  *value = d.EncodeToComparable();
  return Status::OK();
}

CHECKED_STATUS PTLiteralString::ToVarInt(string *value, bool negate) const {
  util::VarInt v;
  if (negate) {
    RETURN_NOT_OK(v.FromString(string("-") + value_->c_str()));
  } else {
    RETURN_NOT_OK(v.FromString(value_->c_str()));
  }
  *value = v.EncodeToComparable();
  return Status::OK();
}

CHECKED_STATUS PTLiteralString::ToString(string *value) const {
  *value = value_->c_str();
  return Status::OK();
}

CHECKED_STATUS PTLiteralString::ToTimestamp(int64_t *value) const {
  auto ts = DateTime::TimestampFromString(value_->c_str());
  RETURN_NOT_OK(ts);
  *value = ts->ToInt64();
  return Status::OK();
}

CHECKED_STATUS PTLiteralString::ToInetaddress(InetAddress *value) const {
  RETURN_NOT_OK(value->FromString(value_->c_str()));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Collections.

CHECKED_STATUS PTCollectionExpr::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(CheckOperator(sem_context));
  const shared_ptr<QLType>& expected_type = sem_context->expr_expected_ql_type();

  // If no expected type is given, use type inferred during parsing
  if (expected_type->main() == DataType::UNKNOWN_DATA) {
    return CheckExpectedTypeCompatibility(sem_context);
  }

  // Ensuring expected type is compatible with parsing/literal type.
  auto conversion_mode = QLType::GetConversionMode(expected_type->main(), ql_type_->main());
  if (conversion_mode > QLType::ConversionMode::kFurtherCheck) {
    return sem_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
  }

  const MCSharedPtr<MCString>& bindvar_name = sem_context->bindvar_name();

  // Checking type parameters.
  switch (expected_type->main()) {
    case MAP: {
      if (ql_type_->main() == SET && values_.size() > 0) {
        return sem_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
      }
      SemState sem_state(sem_context);

      const shared_ptr<QLType>& key_type = expected_type->param_type(0);
      sem_state.SetExprState(key_type, YBColumnSchema::ToInternalDataType(key_type), bindvar_name);
      for (auto& key : keys_) {
        RETURN_NOT_OK(key->Analyze(sem_context));
      }

      const shared_ptr<QLType>& val_type = expected_type->param_type(1);
      sem_state.SetExprState(val_type, YBColumnSchema::ToInternalDataType(val_type), bindvar_name);
      for (auto& value : values_) {
        RETURN_NOT_OK(value->Analyze(sem_context));
      }

      sem_state.ResetContextState();
      break;
    }
    case SET: {
      SemState sem_state(sem_context);
      const shared_ptr<QLType>& val_type = expected_type->param_type(0);
      sem_state.SetExprState(val_type, YBColumnSchema::ToInternalDataType(val_type), bindvar_name);
      for (auto& elem : values_) {
        RETURN_NOT_OK(elem->Analyze(sem_context));
      }
      sem_state.ResetContextState();
      break;
    }

    case LIST: {
      SemState sem_state(sem_context);
      const shared_ptr<QLType>& val_type = expected_type->param_type(0);
      sem_state.SetExprState(val_type, YBColumnSchema::ToInternalDataType(val_type), bindvar_name);
      for (auto& elem : values_) {
        RETURN_NOT_OK(elem->Analyze(sem_context));
      }
      sem_state.ResetContextState();
      break;
    }

    case USER_DEFINED_TYPE: {
      SemState sem_state(sem_context);
      DCHECK_EQ(keys_.size(), values_.size());

      udtype_field_values_.resize(expected_type->udtype_field_names().size());
      // Each literal key/value pair must correspond to a field name/type pair from the UDT
      auto values_it = values_.begin();
      for (auto& key : keys_) {
        // All keys must be field refs

        // TODO (mihnea) Consider unifying handling of field references (for user-defined types) and
        // column references (for tables) to simplify this path.
        PTRef* field_ref = dynamic_cast<PTRef *>(key.get());
        if (field_ref == nullptr) {
          return sem_context->Error(this,
                                    "Field names for user-defined types must be field reference",
                                    ErrorCode::INVALID_ARGUMENTS);
        }
        if (!field_ref->name()->IsSimpleName()) {
          return sem_context->Error(this,
                                    "Qualified names not allowed for fields of user-defined types",
                                    ErrorCode::INVALID_ARGUMENTS);
        }
        string field_name(field_ref->name()->last_name().c_str());

        // All keys must be existing field names from the UDT
        int field_idx = expected_type->GetUDTypeFieldIdxByName(field_name);
        if (field_idx < 0) {
          return sem_context->Error(this, "Invalid field name found for user-defined type instance",
                                    ErrorCode::INVALID_ARGUMENTS);
        }

        // Setting the corresponding field value
        udtype_field_values_[field_idx] = *values_it;

        // Each value should have the corresponding type from the UDT
        auto& param_type = expected_type->param_type(field_idx);
        sem_state.SetExprState(param_type,
                               YBColumnSchema::ToInternalDataType(param_type),
                               bindvar_name);
        RETURN_NOT_OK(udtype_field_values_[field_idx]->Analyze(sem_context));

        values_it++;
      }
      sem_state.ResetContextState();
      break;
    }

    case FROZEN: {
      SemState sem_state(sem_context);
      sem_state.SetExprState(expected_type->param_type(0),
                             YBColumnSchema::ToInternalDataType(expected_type->param_type(0)),
                             bindvar_name);
      RETURN_NOT_OK(Analyze(sem_context));
      sem_state.ResetContextState();
      break;
    }

    case TUPLE:
      return sem_context->Error(this, "Tuple type not supported yet",
                                ErrorCode::FEATURE_NOT_SUPPORTED);

    default:
      return sem_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
  }

  // Assign correct datatype.
  ql_type_ = expected_type;
  internal_type_ = sem_context->expr_expected_internal_type();

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Logic expressions consist of the following operators.
//   ExprOperator::kNot
//   ExprOperator::kAND
//   ExprOperator::kOR
//   ExprOperator::kIsTrue
//   ExprOperator::kIsFalse

CHECKED_STATUS PTLogicExpr::SetupSemStateForOp1(SemState *sem_state) {
  // Expect "bool" datatype for logic expression.
  sem_state->SetExprState(QLType::Create(BOOL), InternalType::kBoolValue);

  // If this is OP_AND, we need to pass down the state variables for where clause "where_state".
  if (ql_op_ == QL_OP_AND) {
    sem_state->CopyPreviousWhereState();
  }
  return Status::OK();
}

CHECKED_STATUS PTLogicExpr::SetupSemStateForOp2(SemState *sem_state) {
  // Expect "bool" datatype for logic expression.
  sem_state->SetExprState(QLType::Create(BOOL), InternalType::kBoolValue);

  // If this is OP_AND, we need to pass down the state variables for where clause "where_state".
  if (ql_op_ == QL_OP_AND) {
    sem_state->CopyPreviousWhereState();
  }
  return Status::OK();
}

CHECKED_STATUS PTLogicExpr::AnalyzeOperator(SemContext *sem_context,
                                            PTExpr::SharedPtr op1) {
  switch (ql_op_) {
    case QL_OP_NOT:
      if (op1->ql_type_id() != BOOL) {
        return sem_context->Error(this, "Only boolean value is allowed in this context",
                                  ErrorCode::INVALID_DATATYPE);
      }
      internal_type_ = yb::InternalType::kBoolValue;
      break;

    case QL_OP_IS_TRUE: FALLTHROUGH_INTENDED;
    case QL_OP_IS_FALSE:
      return sem_context->Error(this, "Operator not supported yet",
                                ErrorCode::CQL_STATEMENT_INVALID);
    default:
      LOG(FATAL) << "Invalid operator";
  }
  return Status::OK();
}

CHECKED_STATUS PTLogicExpr::AnalyzeOperator(SemContext *sem_context,
                                            PTExpr::SharedPtr op1,
                                            PTExpr::SharedPtr op2) {
  // Verify the operators.
  DCHECK(ql_op_ == QL_OP_AND || ql_op_ == QL_OP_OR);

  // "op1" and "op2" must have been analyzed before getting here
  if (op1->ql_type_id() != BOOL) {
    return sem_context->Error(op1, "Only boolean value is allowed in this context",
                              ErrorCode::INVALID_DATATYPE);
  }
  if (op2->ql_type_id() != BOOL) {
    return sem_context->Error(op2, "Only boolean value is allowed in this context",
                              ErrorCode::INVALID_DATATYPE);
  }

  internal_type_ = yb::InternalType::kBoolValue;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Relations expressions: ==, !=, >, >=, between, ...

CHECKED_STATUS PTRelationExpr::SetupSemStateForOp1(SemState *sem_state) {
  // passing down where state
  sem_state->CopyPreviousWhereState();
  // No expectation for operand 1. All types are accepted.
  return Status::OK();
}

CHECKED_STATUS PTRelationExpr::SetupSemStateForOp2(SemState *sem_state) {
  // The state of operand2 is dependent on operand1.
  PTExpr::SharedPtr operand1 = op1();
  DCHECK(operand1 != nullptr);

  switch (ql_op_) {
    case QL_OP_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_EXISTS: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EXISTS: FALLTHROUGH_INTENDED;
    case QL_OP_BETWEEN: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_BETWEEN: {
      if (operand1->expr_op() == ExprOperator::kRef) {
        const PTRef *ref = static_cast<const PTRef *>(operand1.get());
        sem_state->SetExprState(ref->ql_type(),
            ref->internal_type(),
            ref->bindvar_name(),
            ref->desc());
      } else {
        sem_state->SetExprState(operand1->ql_type(), operand1->internal_type());
      }

      if (operand1->expr_op() == ExprOperator::kBcall) {
        PTBcall *bcall = static_cast<PTBcall *>(operand1.get());
        if (strcmp(bcall->name()->c_str(), "token") == 0) {
          sem_state->set_bindvar_name(PTBindVar::token_bindvar_name());
        }
      }
      break;
    }

    case QL_OP_IN: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN: {
      auto ql_type = QLType::CreateTypeList(operand1->ql_type());

      if (operand1->expr_op() == ExprOperator::kRef) {
        const PTRef *ref = static_cast<const PTRef *>(operand1.get());
        sem_state->SetExprState(ql_type,
            ref->internal_type(),
            ref->bindvar_name(),
            ref->desc());
      } else {
        sem_state->SetExprState(ql_type, operand1->internal_type());
      }
      break;
    }

    default:
      LOG(FATAL) << "Invalid operator" << int(ql_op_);
  }

  return Status::OK();
}

CHECKED_STATUS PTRelationExpr::SetupSemStateForOp3(SemState *sem_state) {
  // The states of operand3 is dependent on operand1 in the same way as op2.
  return SetupSemStateForOp2(sem_state);
}

CHECKED_STATUS PTRelationExpr::AnalyzeOperator(SemContext *sem_context) {
  switch (ql_op_) {
    case QL_OP_EXISTS: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EXISTS:
      return Status::OK();
    default:
      LOG(FATAL) << "Invalid operator";
  }
  return Status::OK();
}

CHECKED_STATUS PTRelationExpr::AnalyzeOperator(SemContext *sem_context,
                                               PTExpr::SharedPtr op1) {
  // "op1" must have been analyzed before getting here
  switch (ql_op_) {
    case QL_OP_IS_NULL: FALLTHROUGH_INTENDED;
    case QL_OP_IS_NOT_NULL:
      return sem_context->Error(this, "Operator not supported yet",
                                ErrorCode::CQL_STATEMENT_INVALID);
    default:
      LOG(FATAL) << "Invalid operator" << int(ql_op_);
  }

  return Status::OK();
}

CHECKED_STATUS PTRelationExpr::AnalyzeOperator(SemContext *sem_context,
                                               PTExpr::SharedPtr op1,
                                               PTExpr::SharedPtr op2) {
  // "op1" and "op2" must have been analyzed before getting here
  switch (ql_op_) {
    case QL_OP_EQUAL:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(CheckEqualityOperands(sem_context, op1, op2));
      internal_type_ = yb::InternalType::kBoolValue;
      break;
    case QL_OP_LESS_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EQUAL:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(CheckInequalityOperands(sem_context, op1, op2));
      internal_type_ = yb::InternalType::kBoolValue;
      break;

    case QL_OP_IN:
    case QL_OP_NOT_IN:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      break;

    default:
      return sem_context->Error(this, "Operator not supported yet",
          ErrorCode::CQL_STATEMENT_INVALID);
  }

  WhereExprState *where_state = sem_context->where_state();
  if (where_state != nullptr) {
    // CheckLhsExpr already checks that this is either kRef or kBcall
    DCHECK(op1->expr_op() == ExprOperator::kRef ||
           op1->expr_op() == ExprOperator::kSubColRef ||
           op1->expr_op() == ExprOperator::kBcall);
    if (op1->expr_op() == ExprOperator::kRef) {
      const PTRef *ref = static_cast<const PTRef *>(op1.get());
      return where_state->AnalyzeColumnOp(sem_context, this, ref->desc(), op2);
    } else if (op1->expr_op() == ExprOperator::kSubColRef) {
      const PTSubscriptedColumn *ref = static_cast<const PTSubscriptedColumn *>(op1.get());
      return where_state->AnalyzeColumnOp(sem_context, this, ref->desc(), op2, ref->args());
    } else if (op1->expr_op() == ExprOperator::kBcall) {
      const PTBcall *bcall = static_cast<const PTBcall *>(op1.get());
      if (strcmp(bcall->name()->c_str(), "token") == 0) {
        const PTToken *token = static_cast<const PTToken *>(bcall);
        if (token->is_partition_key_ref()) {
          return where_state->AnalyzePartitionKeyOp(sem_context, this, op2);
        } else {
          return sem_context->Error(this, "Only token calls that reference partition key allowed",
              ErrorCode::FEATURE_NOT_SUPPORTED);
        }
      } else if (strcmp(bcall->name()->c_str(), "ttl") == 0 ||
                 strcmp(bcall->name()->c_str(), "writetime") == 0) {
        PTBcall::SharedPtr ttl_shared = std::make_shared<PTBcall>(*bcall);
        return where_state->AnalyzeColumnFunction(sem_context, this, op2, ttl_shared);
      } else {
        return sem_context->Error(loc(), "Builtin call not allowed in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
    }
  }
  return Status::OK();
}

CHECKED_STATUS PTRelationExpr::AnalyzeOperator(SemContext *sem_context,
                                               PTExpr::SharedPtr op1,
                                               PTExpr::SharedPtr op2,
                                               PTExpr::SharedPtr op3) {
  // "op1", "op2", and "op3" must have been analyzed before getting here
  switch (ql_op_) {
    case QL_OP_BETWEEN: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_BETWEEN:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(op3->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(CheckInequalityOperands(sem_context, op1, op2));
      RETURN_NOT_OK(CheckInequalityOperands(sem_context, op1, op3));
      internal_type_ = yb::InternalType::kBoolValue;
      break;

    default:
      LOG(FATAL) << "Invalid operator " << QLOperator_Name(ql_op_);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTOperatorExpr::SetupSemStateForOp1(SemState *sem_state) {
  switch (op_) {
    case ExprOperator::kUMinus:
    case ExprOperator::kAlias:
      sem_state->CopyPreviousStates();
      break;

    default:
      LOG(FATAL) << "Invalid operator " << int(op_);
  }

  return Status::OK();
}

CHECKED_STATUS PTOperatorExpr::AnalyzeOperator(SemContext *sem_context,
                                               PTExpr::SharedPtr op1) {
  switch (op_) {
    case ExprOperator::kUMinus:
      // "op1" must have been analyzed before we get here.
      // Check to make sure that it is allowed in this context.
      if (op1->expr_op() != ExprOperator::kConst) {
        return sem_context->Error(this, "Only numeric constant is allowed in this context",
                                  ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      if (!QLType::IsNumeric(op1->ql_type_id())) {
        return sem_context->Error(this, "Only numeric data type is allowed in this context",
                                  ErrorCode::INVALID_DATATYPE);
      }

      // Type resolution: (-x) should have the same datatype as (x).
      ql_type_ = op1->ql_type();
      internal_type_ = op1->internal_type();
      break;

    default:
      LOG(FATAL) << "Invalid operator" << int(op_);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTRef::PTRef(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTQualifiedName::SharedPtr& name)
    : PTOperator0(memctx, loc, ExprOperator::kRef, yb::QLOperator::QL_OP_NOOP),
      name_(name),
      desc_(nullptr) {
}

PTRef::~PTRef() {
}

CHECKED_STATUS PTRef::AnalyzeOperator(SemContext *sem_context) {
  DCHECK(name_ != nullptr) << "Reference column is not specified";

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  if (!name_->IsSimpleName()) {
    return sem_context->Error(this, "Qualified name not allowed for column reference",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }
  desc_ = sem_context->GetColumnDesc(name_->last_name());
  if (desc_ == nullptr) {
    return sem_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  internal_type_ = desc_->internal_type();
  ql_type_ = desc_->ql_type();

  return Status::OK();
}

CHECKED_STATUS PTRef::CheckLhsExpr(SemContext *sem_context) {
  // When CQL IF clause is being processed. In that case, disallow reference to primary key columns
  // and counters.
  if (sem_context->processing_if_clause()) {
    if (desc_->is_primary()) {
      return sem_context->Error(this, "Primary key column reference is not allowed in if clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
    } else if (desc_->is_counter()) {
      return sem_context->Error(this, "Counter column reference is not allowed in if clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
  }
  return Status::OK();
}

void PTRef::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTSubscriptedColumn::PTSubscriptedColumn(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTQualifiedName::SharedPtr& name,
             const PTExprListNode::SharedPtr& args)
    : PTOperator0(memctx, loc, ExprOperator::kSubColRef, yb::QLOperator::QL_OP_NOOP),
      name_(name),
      args_(args),
      desc_(nullptr) {
}

PTSubscriptedColumn::~PTSubscriptedColumn() {
}

CHECKED_STATUS PTSubscriptedColumn::AnalyzeOperator(SemContext *sem_context) {

  // Check if this refers to the whole table (SELECT *).
  if (name_ == nullptr) {
    return sem_context->Error(this, "Cannot do type resolution for wildcard reference (SELECT *)",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  desc_ = sem_context->GetColumnDesc(name_->last_name());
  if (desc_ == nullptr) {
    return sem_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  SemState sem_state(sem_context);

  auto curr_ytype = desc_->ql_type();
  auto curr_itype = desc_->internal_type();

  if (args_ != nullptr) {
    for (const auto &arg : args_->node_list()) {
      if (curr_ytype->keys_type() == nullptr) {
        return sem_context->Error(this, "Columns with elementary types cannot take arguments",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }

      sem_state.SetExprState(curr_ytype->keys_type(),
          client::YBColumnSchema::ToInternalDataType(curr_ytype->keys_type()));
          RETURN_NOT_OK(arg->Analyze(sem_context));

      curr_ytype = curr_ytype->values_type();
      curr_itype = client::YBColumnSchema::ToInternalDataType(curr_ytype);
    }
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  ql_type_ = curr_ytype;
  internal_type_ = curr_itype;

  return Status::OK();
}

CHECKED_STATUS PTSubscriptedColumn::CheckLhsExpr(SemContext *sem_context) {
  // If where_state is null, we are processing the IF clause. In that case, disallow reference to
  // primary key columns.
  if (sem_context->where_state() == nullptr && desc_->is_primary()) {
    return sem_context->Error(this, "Primary key column reference is not allowed in if expression",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

void PTSubscriptedColumn::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTAllColumns::PTAllColumns(MemoryContext *memctx,
                           YBLocation::SharedPtr loc)
    : PTOperator0(memctx, loc, ExprOperator::kRef, yb::QLOperator::QL_OP_NOOP),
      stmt_(nullptr) {
}

PTAllColumns::~PTAllColumns() {
}

CHECKED_STATUS PTAllColumns::AnalyzeOperator(SemContext *sem_context) {
  // Make sure '*' is used only in 'SELECT *' statement.
  PTDmlStmt *stmt = sem_context->current_dml_stmt();
  if (stmt == nullptr ||
      stmt->opcode() != TreeNodeOpcode::kPTSelectStmt ||
      static_cast<PTSelectStmt*>(stmt)->selected_exprs().size() > 1) {
    return sem_context->Error(loc(), "Cannot use '*' expression in this context",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  stmt_ = static_cast<PTSelectStmt*>(stmt);

  // Note to server that all column are referenced by this statement.
  sem_context->current_dml_stmt()->AddRefForAllColumns();

  // TODO(Mihnea) See if TUPLE datatype can be used here.
  // '*' should be of TUPLE type, but we use the following workaround for now.
  ql_type_ = QLType::Create(DataType::NULL_VALUE_TYPE);
  internal_type_ = InternalType::kListValue;
  return Status::OK();
}

const MCVector<ColumnDesc>& PTAllColumns::table_columns() const {
  return stmt_->table_columns();
}

//--------------------------------------------------------------------------------------------------

PTExprAlias::PTExprAlias(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         const PTExpr::SharedPtr& expr,
                         const MCSharedPtr<MCString>& alias)
    : PTOperator1(memctx, loc, ExprOperator::kAlias, yb::QLOperator::QL_OP_NOOP, expr),
      alias_(alias) {
}

PTExprAlias::~PTExprAlias() {
}

CHECKED_STATUS PTExprAlias::SetupSemStateForOp1(SemState *sem_state) {
  sem_state->set_allowing_aggregate(sem_state->previous_state()->allowing_aggregate());
  return Status::OK();
}

CHECKED_STATUS PTExprAlias::AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) {
  // Type resolution: Alias of (x) should have the same datatype as (x).
  ql_type_ = op1->ql_type();
  internal_type_ = op1->internal_type();
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTBindVar::PTBindVar(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const MCSharedPtr<MCString>& name)
    : PTExpr(memctx, loc, ExprOperator::kBindVar),
      user_pos_(nullptr),
      pos_(kUnsetPosition),
      name_(name) {
}

PTBindVar::PTBindVar(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     PTConstVarInt::SharedPtr user_pos)
    : PTExpr(memctx, loc, ExprOperator::kBindVar),
      user_pos_(user_pos),
      pos_(kUnsetPosition),
      name_(nullptr) {
}

PTBindVar::~PTBindVar() {
}

CHECKED_STATUS PTBindVar::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(CheckOperator(sem_context));

  if (name_ == nullptr) {
    name_ = sem_context->bindvar_name();
  }

  if (user_pos_ != nullptr) {
    if (!user_pos_->ToInt64(&pos_, false).ok()) {
      return sem_context->Error(this, "Bind position is invalid!",
                                ErrorCode::INVALID_ARGUMENTS);
    }

    if (pos_ <= 0) {
      return sem_context->Error(this, "Bind variable position should be positive!",
                                ErrorCode::INVALID_ARGUMENTS);
    }
    // Convert from 1 based to 0 based.
    pos_--;
  }

  if (sem_context->expr_expected_ql_type()->main() == DataType::UNKNOWN_DATA) {
    // By default bind variables are compatible with any type.
    ql_type_ = QLType::Create(NULL_VALUE_TYPE);
  } else {
    ql_type_ = sem_context->expr_expected_ql_type();
  }

  internal_type_ = sem_context->expr_expected_internal_type();
  expected_internal_type_ = internal_type_;
  hash_col_ = sem_context->hash_col();
  if (hash_col_ != nullptr) {
    DCHECK(sem_context->current_dml_stmt() != nullptr);
    sem_context->current_dml_stmt()->AddHashColumnBindVar(this);
  }

  return Status::OK();
}

void PTBindVar::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace ql
}  // namespace yb
