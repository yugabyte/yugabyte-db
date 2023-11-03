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

#include "yb/bfql/tserver_opcodes.h"

#include "yb/client/table.h"

#include "yb/common/common.pb.h"
#include "yb/common/value.messages.h"
#include "yb/common/value.pb.h"
#include "yb/qlexpr/index.h"
#include "yb/qlexpr/index_column.h"
#include "yb/common/ql_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/logging.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/net/net_util.h"
#include "yb/util/stol_utils.h"

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

namespace yb {
namespace ql {

using client::YBColumnSchema;
using std::shared_ptr;
using std::string;

using qlexpr::QLNameOption;

//--------------------------------------------------------------------------------------------------

PTExpr::PTExpr(
    MemoryContext *memctx,
    YBLocation::SharedPtr loc,
    ExprOperator op,
    yb::QLOperator ql_op,
    InternalType internal_type,
    DataType ql_type_id)
    : PTExpr(memctx, loc, op, ql_op, internal_type, QLType::Create(ql_type_id)) {
}

PTExpr::PTExpr(
    MemoryContext *memctx,
    YBLocation::SharedPtr loc,
    ExprOperator op,
    yb::QLOperator ql_op,
    InternalType internal_type,
    const QLTypePtr& ql_type)
    : TreeNode(memctx, loc),
      op_(op),
      ql_op_(ql_op),
      internal_type_(internal_type),
      ql_type_(ql_type),
      expected_internal_type_(InternalType::VALUE_NOT_SET),
      is_in_operand_(false),
      index_name_(MCMakeShared<MCString>(memctx)) {
}

void PTExpr::set_ql_type(DataType type_id) {
  ql_type_ = QLType::Create(type_id);
}

void PTExpr::set_ql_type_id(DataType type_id) {
  ql_type_ = QLType::Create(type_id);
}

DataType PTExpr::ql_type_id() const {
  return ql_type_->main();
}

bool PTExpr::has_valid_ql_type_id() {
  return ql_type_->main() != DataType::UNKNOWN_DATA;
}

bfql::TSOpcode PTExpr::aggregate_opcode() const {
  return bfql::TSOpcode::kNoOp;
}

bool PTExpr::is_null() const {
  return ql_type_->main() == DataType::NULL_VALUE_TYPE;
}

std::string PTExpr::MetadataName() const {
  // If this expression was used to define an index column, use its descriptor name.
  return index_desc_ ? index_desc_->MetadataName() : QLName(QLNameOption::kMetadataName);
}

bool PTExpr::has_valid_internal_type() {
  // internal_type_ is not set in case of PTNull.
  return ql_type_->main() == DataType::NULL_VALUE_TYPE ||
         internal_type_ != InternalType::VALUE_NOT_SET;
}

void PTExpr::rscol_type_PB(QLTypePB *pb_type ) const {
  ql_type_->ToQLTypePB(pb_type);
}

bool PTExpr::CheckIndexColumn(SemContext *sem_context) {
  if (!sem_context->selecting_from_index()) {
    return false;
  }

  // Currently, only PTJsonColumnWithOperators node is allowed to be an IndexColumn. However, define
  // this analysis in PTExpr class so that it's easier to extend the support INDEX by expression.
  if (op_ != ExprOperator::kJsonOperatorRef) {
    return false;
  }

  // Check if this expression is used for indexing.
  index_desc_ = GetColumnDesc(sem_context);
  if (index_desc_ != nullptr) {
    // Type resolution: This expr should have the same datatype as the column.
    index_name_->assign(QLName().c_str());
    internal_type_ = index_desc_->internal_type();
    ql_type_ = index_desc_->ql_type();
    return true;
  }

  return false;
}

Status PTExpr::CheckOperator(SemContext *sem_context) {
  // Where clause only allow AND, EQ, LT, LE, GT, GE, CONTAINS AND CONTAINS KEY operators.
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
      case QL_OP_NOT_EQUAL:
      case QL_OP_CONTAINS_KEY:
      case QL_OP_CONTAINS:
      case QL_OP_NOOP:
        break;
      default:
        return sem_context->Error(this, "This operator is not allowed in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  // Partial index where clause only supports these operators: =, AND, !=, >, <, >=, <=.
  if (sem_context->idx_predicate_state() != nullptr) {
    switch (ql_op_) {
      case QL_OP_AND:
      case QL_OP_EQUAL:
      case QL_OP_NOT_EQUAL:
      case QL_OP_GREATER_THAN:
      case QL_OP_GREATER_THAN_EQUAL:
      case QL_OP_LESS_THAN:
      case QL_OP_LESS_THAN_EQUAL:
      case QL_OP_NOOP:
        break;
      default:
        return sem_context->Error(this,
                                  "This operator is not allowed in partial index where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
    }
  }
  return Status::OK();
}

Status PTExpr::AnalyzeOperator(SemContext *sem_context) {
  return Status::OK();
}

Status PTExpr::AnalyzeOperator(SemContext *sem_context,
                               PTExpr::SharedPtr op1) {
  return Status::OK();
}

Status PTExpr::AnalyzeOperator(SemContext *sem_context,
                               PTExpr::SharedPtr op1,
                               PTExpr::SharedPtr op2) {
  return Status::OK();
}

Status PTExpr::AnalyzeOperator(SemContext *sem_context,
                               PTExpr::SharedPtr op1,
                               PTExpr::SharedPtr op2,
                               PTExpr::SharedPtr op3) {
  return Status::OK();
}

Status PTExpr::SetupSemStateForOp1(SemState *sem_state) {
  return Status::OK();
}

Status PTExpr::SetupSemStateForOp2(SemState *sem_state) {
  // Passing down where clause state variables.
  return Status::OK();
}

Status PTExpr::SetupSemStateForOp3(SemState *sem_state) {
  return Status::OK();
}

Status PTExpr::CheckExpectedTypeCompatibility(SemContext *sem_context) {
  CHECK(has_valid_internal_type() && has_valid_ql_type_id());

  // Check if RHS accepts NULL.
  if (ql_type_->main() == DataType::NULL_VALUE_TYPE &&
      !sem_context->expected_ql_type_accepts_null()) {
    return sem_context->Error(this, ErrorCode::NULL_IN_COLLECTIONS);
  }

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
Status PTExpr::CheckInequalityOperands(SemContext *sem_context,
                                       PTExpr::SharedPtr lhs,
                                       PTExpr::SharedPtr rhs) {
  if (!sem_context->IsComparable(lhs->ql_type_id(), rhs->ql_type_id())) {
    return sem_context->Error(this, "Cannot compare values of these datatypes",
                              ErrorCode::INCOMPARABLE_DATATYPES);
  }
  return Status::OK();
}

Status PTExpr::CheckEqualityOperands(SemContext *sem_context,
                                     PTExpr::SharedPtr lhs,
                                     PTExpr::SharedPtr rhs) {
  if (QLType::IsNull(lhs->ql_type_id()) || QLType::IsNull(rhs->ql_type_id())) {
    return Status::OK();
  } else {
    return CheckInequalityOperands(sem_context, lhs, rhs);
  }
}


Status PTExpr::CheckLhsExpr(SemContext *sem_context) {
  if (op_ != ExprOperator::kRef && op_ != ExprOperator::kBcall &&
      op_ != ExprOperator::kCollection) {
    return sem_context->Error(
        this,
        "Only column refs, collections of column refs and builtin calls are allowed for left hand "
        "value",
        ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

Status PTExpr::CheckRhsExpr(SemContext *sem_context) {
  // Check for limitation in QL (Not all expressions are acceptable).
  switch (op_) {
    case ExprOperator::kRef:
      // Only accept column references where they are explicitly allowed.
      if (sem_context->sem_state() == nullptr || !sem_context->allowing_column_refs()) {
        return sem_context->Error(this,
                                  "Column references are not allowed in this context",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      FALLTHROUGH_INTENDED;
    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kCollection: FALLTHROUGH_INTENDED;
    case ExprOperator::kUMinus: FALLTHROUGH_INTENDED;
    case ExprOperator::kBindVar: FALLTHROUGH_INTENDED;
    case ExprOperator::kJsonOperatorRef: FALLTHROUGH_INTENDED;
    case ExprOperator::kBcall:
      break;
    default:
      return sem_context->Error(this, "Operator not allowed as right hand value",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }

  return Status::OK();
}

Status PTExpr::CheckCounterUpdateSupport(SemContext *sem_context) const {
  return sem_context->Error(this, ErrorCode::INVALID_COUNTING_EXPR);
}

PTExpr::SharedPtr PTExpr::CreateConst(MemoryContext *memctx,
                                      YBLocation::SharedPtr loc,
                                      PTBaseType::SharedPtr data_type) {
  switch(data_type->ql_type()->main()) {
    case DataType::DOUBLE:
      return PTConstDouble::MakeShared(memctx, loc, 0);
    case DataType::FLOAT:
      return PTConstFloat::MakeShared(memctx, loc, 0);
    case DataType::INT16:
      return PTConstInt16::MakeShared(memctx, loc, 0);
    case DataType::INT32:
      return PTConstInt32::MakeShared(memctx, loc, 0);
    case DataType::INT64:
      return PTConstInt::MakeShared(memctx, loc, 0);
    case DataType::STRING:
      return PTConstText::MakeShared(memctx, loc, MCMakeShared<MCString>(memctx, ""));
    case DataType::TIMESTAMP:
      return PTConstTimestamp::MakeShared(memctx, loc, 0);
    case DataType::DATE:
      return PTConstDate::MakeShared(memctx, loc, 0);
    case DataType::DECIMAL:
      return PTConstDecimal::MakeShared(memctx, loc, MCMakeShared<MCString>(memctx));
    default:
      LOG(WARNING) << "Unexpected QL type: " << data_type->ql_type()->ToString();
      return nullptr;
  }
}

//--------------------------------------------------------------------------------------------------

PTLiteralString::PTLiteralString(MCSharedPtr<MCString> value)
    : PTLiteral<MCSharedPtr<MCString>>(value) {
}

PTLiteralString::~PTLiteralString() {
}

Status PTLiteralString::ToInt64(int64_t *value, bool negate) const {
  auto temp = negate ? CheckedStoll(string("-") + value_->c_str())
              : CheckedStoll(*value_);
  RETURN_NOT_OK(temp);
  *value = *temp;
  return Status::OK();
}

Status PTLiteralString::ToDouble(long double *value, bool negate) const {
  auto temp = CheckedStold(*value_);
  RETURN_NOT_OK(temp);
  *value = negate ? -*temp : *temp;
  return Status::OK();
}

Status PTLiteralString::ToDecimal(util::Decimal *value, bool negate) const {
  if (negate) {
    return value->FromString(string("-") + value_->c_str());
  } else {
    return value->FromString(value_->c_str());
  }
}

Status PTLiteralString::ToDecimal(string *value, bool negate) const {
  util::Decimal d;
  if (negate) {
    RETURN_NOT_OK(d.FromString(string("-") + value_->c_str()));
  } else {
    RETURN_NOT_OK(d.FromString(value_->c_str()));
  }
  *value = d.EncodeToComparable();
  return Status::OK();
}

Status PTLiteralString::ToVarInt(string *value, bool negate) const {
  VarInt v;
  if (negate) {
    RETURN_NOT_OK(v.FromString(string("-") + value_->c_str()));
  } else {
    RETURN_NOT_OK(v.FromString(value_->c_str()));
  }
  *value = v.EncodeToComparable();
  return Status::OK();
}

std::string PTLiteralString::ToString() const {
  return string(value_->c_str());
}

Status PTLiteralString::ToString(string *value) const {
  *value = value_->c_str();
  return Status::OK();
}

Status PTLiteralString::ToTimestamp(int64_t *value) const {
  *value = VERIFY_RESULT(DateTime::TimestampFromString(value_->c_str())).ToInt64();
  return Status::OK();
}

Status PTLiteralString::ToDate(uint32_t *value) const {
  *value = VERIFY_RESULT(DateTime::DateFromString(value_->c_str()));
  return Status::OK();
}

Status PTLiteralString::ToTime(int64_t *value) const {
  *value = VERIFY_RESULT(DateTime::TimeFromString(value_->c_str()));
  return Status::OK();
}

Status PTLiteralString::ToInetaddress(InetAddress *value) const {
  *value = InetAddress(VERIFY_RESULT(HostToAddress(value_->c_str())));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Collections.

Status PTCollectionExpr::InitializeUDTValues(const QLType::SharedPtr& expected_type,
                                             ProcessContextBase* process_context) {
  SCHECK(expected_type->IsUserDefined(), Corruption, "Expected type should be UDT");
  SCHECK_EQ(keys_.size(), values_.size(), Corruption,
            "Expected keys and values to be of the same size");

  udtype_field_values_.resize(expected_type->udtype_field_names().size());
  // Each literal key/value pair must correspond to a field name/type pair from the UDT
  auto values_it = values_.begin();
  for (const auto& key : keys_) {
    // All keys must be field refs

    // TODO (mihnea) Consider unifying handling of field references (for user-defined types) and
    // column references (for tables) to simplify this path.
    if (key->opcode() != TreeNodeOpcode::kPTRef) {
      return process_context->Error(this,
                                    "Field names for user-defined types must be field reference",
                                    ErrorCode::INVALID_ARGUMENTS);
    }
    PTRef* field_ref = down_cast<PTRef*>(key.get());
    if (!field_ref->name()->IsSimpleName()) {
      return process_context->Error(this,
                                    "Qualified names not allowed for fields of user-defined types",
                                    ErrorCode::INVALID_ARGUMENTS);
    }
    string field_name(field_ref->name()->last_name().c_str());

    // All keys must be existing field names from the UDT
    auto field_idx = expected_type->GetUDTypeFieldIdxByName(field_name);
    if (!field_idx) {
      return process_context->Error(this, "Invalid field name found for user-defined type instance",
                                    ErrorCode::INVALID_ARGUMENTS);
    }

    // Setting the corresponding field value
    udtype_field_values_[*field_idx] = *values_it;
    values_it++;
  }
  return Status::OK();
}

PTCollectionExpr::PTCollectionExpr(MemoryContext* memctx,
                 YBLocationPtr loc,
                 const QLTypePtr& ql_type)
    : PTExpr(memctx, loc, ExprOperator::kCollection, yb::QLOperator::QL_OP_NOOP,
             client::YBColumnSchema::ToInternalDataType(ql_type), ql_type),
      keys_(memctx), values_(memctx), udtype_field_values_(memctx) {}

PTCollectionExpr::PTCollectionExpr(MemoryContext* memctx, YBLocationPtr loc, DataType literal_type)
    : PTCollectionExpr(memctx, loc, QLType::Create(literal_type)) {}

Status PTCollectionExpr::Analyze(SemContext *sem_context) {
  // Before traversing the expression, check if this whole expression is actually a column.
  if (CheckIndexColumn(sem_context)) {
    return Status::OK();
  }

  RETURN_NOT_OK(CheckOperator(sem_context));

  // TODO(arpan): looks clumsy, how to do this more cleanly?
  if (ql_type_->main() == DataType::TUPLE && ql_type_->params().size() != values_.size()) {
    for (const auto &value : values_) {
      PTRef *ref = static_cast<PTRef *>(value.get());
      RETURN_NOT_OK(ref->AnalyzeOperator(sem_context));
      if (ref->desc()) {
        ql_type_->add_param(ref->ql_type());
      }
    }
  }

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
  size_t i = 0;
  // Checking type parameters.
  switch (expected_type->main()) {
    case DataType::MAP: {
      if (ql_type_->main() == DataType::SET && values_.size() > 0) {
        return sem_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
      }

      // Process keys and values.
      // Referencing column in collection is not allowed.
      SemState sem_state(sem_context);
      sem_state.set_allowing_column_refs(false);

      const shared_ptr<QLType>& key_type = expected_type->param_type(0);
      sem_state.SetExprState(
          key_type, YBColumnSchema::ToInternalDataType(key_type),
          bindvar_name, nullptr, NullIsAllowed::kFalse);
      for (auto& key : keys_) {
        RETURN_NOT_OK(key->Analyze(sem_context));
        RETURN_NOT_OK(key->CheckRhsExpr(sem_context));
      }

      const shared_ptr<QLType>& val_type = expected_type->param_type(1);
      sem_state.SetExprState(
          val_type, YBColumnSchema::ToInternalDataType(val_type),
          bindvar_name, nullptr, NullIsAllowed::kFalse);
      for (auto& value : values_) {
        RETURN_NOT_OK(value->Analyze(sem_context));
        RETURN_NOT_OK(value->CheckRhsExpr(sem_context));
      }

      sem_state.ResetContextState();
      break;
    }
    case DataType::SET: {
      // Process values.
      // Referencing column in collection is not allowed.
      SemState sem_state(sem_context);
      sem_state.set_allowing_column_refs(false);

      const shared_ptr<QLType>& val_type = expected_type->param_type(0);
      sem_state.SetExprState(
          val_type, YBColumnSchema::ToInternalDataType(val_type),
          bindvar_name, nullptr, NullIsAllowed::kFalse);
      for (auto& elem : values_) {
        RETURN_NOT_OK(elem->Analyze(sem_context));
        RETURN_NOT_OK(elem->CheckRhsExpr(sem_context));
      }
      sem_state.ResetContextState();
      break;
    }

    case DataType::LIST: {
      // Process values.
      // Referencing column in collection is not allowed.
      SemState sem_state(sem_context);
      sem_state.set_allowing_column_refs(false);

      const shared_ptr<QLType>& val_type = expected_type->param_type(0);
      // NULL value in the LIST is allowed for right operand of IN/NOT IN operators only.
      sem_state.SetExprState(
          val_type, YBColumnSchema::ToInternalDataType(val_type),
          bindvar_name, nullptr, NullIsAllowed(is_in_operand()));
      for (auto& elem : values_) {
        RETURN_NOT_OK(elem->Analyze(sem_context));
        RETURN_NOT_OK(elem->CheckRhsExpr(sem_context));
      }
      sem_state.ResetContextState();
      break;
    }

    case DataType::USER_DEFINED_TYPE: {
      // Process values.
      // Referencing column in collection is not allowed.
      SemState sem_state(sem_context);
      sem_state.set_allowing_column_refs(false);

      RETURN_NOT_OK(InitializeUDTValues(expected_type, sem_context));
      for (size_t i = 0; i < udtype_field_values_.size(); i++) {
        if (!udtype_field_values_[i]) {
          // Skip missing values
          continue;
        }
        // Each value should have the corresponding type from the UDT
        const auto& param_type = expected_type->param_type(i);
        sem_state.SetExprState(param_type,
                               YBColumnSchema::ToInternalDataType(param_type),
                               bindvar_name);
        RETURN_NOT_OK(udtype_field_values_[i]->Analyze(sem_context));
        RETURN_NOT_OK(udtype_field_values_[i]->CheckRhsExpr(sem_context));
      }
      sem_state.ResetContextState();
      break;
    }

    case DataType::FROZEN: {
      if (ql_type_->main() == DataType::FROZEN) {
        // Already analyzed (e.g. for indexes), just check if type matches.
        if (*ql_type_ != *expected_type) {
          return sem_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
        }
      } else {
        SemState sem_state(sem_context);
        sem_state.SetExprState(expected_type->param_type(0),
                               YBColumnSchema::ToInternalDataType(expected_type->param_type(0)),
                               bindvar_name);
        RETURN_NOT_OK(Analyze(sem_context));
        sem_state.ResetContextState();
      }
      break;
    }

    case DataType::TUPLE:
      i = 0;
      if (values_.size() != expected_type->params().size()) {
        return sem_context->Error(
            this,
            Format(
                "Expected $0 elements in value tuple, but got $1", expected_type->params().size(),
                values_.size()),
            ErrorCode::INVALID_ARGUMENTS);
      }
      for (const auto &elem : values_) {
        SemState sem_state(sem_context);
        sem_state.set_allowing_column_refs(false);
        const shared_ptr<QLType> &val_type = expected_type->param_type(i);
        // NULL value in the TUPLE is allowed for right operand of IN/NOT IN operators only.
        sem_state.SetExprState(
            val_type, YBColumnSchema::ToInternalDataType(val_type), bindvar_name, nullptr,
            NullIsAllowed(is_in_operand()));
        RETURN_NOT_OK(elem->Analyze(sem_context));
        RETURN_NOT_OK(elem->CheckRhsExpr(sem_context));
        ++i;
        sem_state.ResetContextState();
      }
      break;
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

Status PTLogicExpr::SetupSemStateForOp1(SemState *sem_state) {
  // Expect "bool" datatype for logic expression.
  sem_state->SetExprState(QLType::Create(DataType::BOOL), InternalType::kBoolValue);

  // Pass down the state variables for IF clause "if_state".
  sem_state->CopyPreviousIfState();

  // If this is OP_AND, we need to pass down the state variables for where clause "where_state".
  if (ql_op_ == QL_OP_AND) {
    sem_state->CopyPreviousWhereState();
  }
  return Status::OK();
}

Status PTLogicExpr::SetupSemStateForOp2(SemState *sem_state) {
  // Expect "bool" datatype for logic expression.
  sem_state->SetExprState(QLType::Create(DataType::BOOL), InternalType::kBoolValue);

  // Pass down the state variables for IF clause "if_state".
  sem_state->CopyPreviousIfState();

  // If this is OP_AND, we need to pass down the state variables for where clause "where_state".
  if (ql_op_ == QL_OP_AND) {
    sem_state->CopyPreviousWhereState();
  }
  return Status::OK();
}

Status PTLogicExpr::AnalyzeOperator(SemContext *sem_context,
                                    PTExpr::SharedPtr op1) {
  switch (ql_op_) {
    case QL_OP_NOT:
      if (op1->ql_type_id() != DataType::BOOL) {
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

Status PTLogicExpr::AnalyzeOperator(SemContext *sem_context,
                                    PTExpr::SharedPtr op1,
                                    PTExpr::SharedPtr op2) {
  // Verify the operators.
  DCHECK(ql_op_ == QL_OP_AND || ql_op_ == QL_OP_OR);

  // "op1" and "op2" must have been analyzed before getting here
  if (op1->ql_type_id() != DataType::BOOL) {
    return sem_context->Error(op1, "Only boolean value is allowed in this context",
                              ErrorCode::INVALID_DATATYPE);
  }
  if (op2->ql_type_id() != DataType::BOOL) {
    return sem_context->Error(op2, "Only boolean value is allowed in this context",
                              ErrorCode::INVALID_DATATYPE);
  }

  internal_type_ = yb::InternalType::kBoolValue;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Relations expressions: ==, !=, >, >=, between, ...

Status PTRelationExpr::SetupSemStateForOp1(SemState *sem_state) {
  // Pass down the state variables for IF clause "if_state".
  sem_state->CopyPreviousIfState();

  // passing down where state
  sem_state->CopyPreviousWhereState();
  sem_state->set_allowing_column_refs(true);
  // No expectation for operand 1. All types are accepted.
  return Status::OK();
}

Status PTRelationExpr::SetupSemStateForOp2(SemState *sem_state) {
  // The state of operand2 is dependent on operand1.
  PTExpr::SharedPtr operand1 = op1();
  DCHECK_NOTNULL(operand1.get());
  sem_state->set_allowing_column_refs(false);

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
      // TODO(neil) Indexing processing should be redesigned such that when processing a statement
      // against an INDEX table, most of these semantic processing shouldn't be done again as they
      // were already done once again the actual table.

      // Setup for expression column.
      if (operand1->index_desc() != nullptr) {
        // Operand1 is a index column.
        sem_state->SetExprState(operand1->ql_type(),
                                operand1->internal_type(),
                                operand1->index_name(),
                                operand1->index_desc());
        break;
      }

      // Setup for table column.
      if (operand1->expr_op() == ExprOperator::kRef) {
        const PTRef *ref = static_cast<const PTRef *>(operand1.get());
        sem_state->SetExprState(ref->ql_type(),
                                ref->internal_type(),
                                ref->bindvar_name(),
                                ref->desc());
        break;
      }

      // Setup for other expression.
      sem_state->SetExprState(operand1->ql_type(), operand1->internal_type());
      switch (operand1->expr_op()) {
        case ExprOperator::kBcall: {
          PTBcall *bcall = static_cast<PTBcall *>(operand1.get());
          DCHECK_NOTNULL(bcall->name().get());
          if (strcmp(bcall->name()->c_str(), "token") == 0) {
            sem_state->set_bindvar_name(PTBindVar::token_bindvar_name());
          }
          if (strcmp(bcall->name()->c_str(), "partition_hash") == 0) {
            sem_state->set_bindvar_name(PTBindVar::partition_hash_bindvar_name());
          }
          break;
        }

        case ExprOperator::kSubColRef: {
          const PTSubscriptedColumn *ref = static_cast<const PTSubscriptedColumn *>(operand1.get());
          if (ref->desc()) {
            sem_state->set_bindvar_name(PTBindVar::coll_value_bindvar_name(ref->desc()->name()));
          } else if (!sem_state->is_uncovered_index_select()) {
            return STATUS(
                QLError, "Column doesn't exist", Slice(), QLError(ErrorCode::UNDEFINED_COLUMN));
          } // else - this column is uncovered by the Index, skip checks and return OK.
          break;
        }

        case ExprOperator::kJsonOperatorRef: {
          const PTJsonColumnWithOperators *ref =
              static_cast<const PTJsonColumnWithOperators*>(operand1.get());
          if (ref->desc()) {
            sem_state->set_bindvar_name(PTBindVar::json_bindvar_name(ref->desc()->name()));
          } else if (!sem_state->is_uncovered_index_select()) {
            return STATUS(
                QLError, "Column doesn't exist", Slice(), QLError(ErrorCode::UNDEFINED_COLUMN));
          } // else - this column is uncovered by the Index, skip checks and return OK.
          break;
        }

        default: {} // Use default bindvar name below.
      }

      break;
    }

    case QL_OP_CONTAINS_KEY: {
      // We will check for data type mismatch errors in AnalyzeOperator
      if (operand1->expr_op() == ExprOperator::kRef &&
          operand1->ql_type()->main() == yb::DataType::MAP) {
        const PTRef *ref = static_cast<const PTRef *>(operand1.get());
        auto ql_type = operand1->ql_type()->keys_type();
        sem_state->SetExprState(
            ql_type,
            client::YBColumnSchema::ToInternalDataType(ql_type),
            ref->bindvar_name(),
            ref->desc());
      }
      break;
    }

    case QL_OP_CONTAINS: {
      // We will check for data type mismatch errors in AnalyzeOperator.
      if (operand1->expr_op() == ExprOperator::kRef && operand1->ql_type()->IsCollection()) {
        const PTRef *ref = static_cast<const PTRef *>(operand1.get());
        auto ql_type = operand1->ql_type()->values_type();
        sem_state->SetExprState(ql_type,
                                client::YBColumnSchema::ToInternalDataType(ql_type),
                                ref->bindvar_name(),
                                ref->desc());
      }
      break;
    }
    case QL_OP_IN: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN: {
      auto ql_type = QLType::CreateTypeList(operand1->ql_type());

      if (operand1->index_desc() != nullptr) {
        // Operand1 is a index column.
        sem_state->SetExprState(operand1->ql_type(),
                                operand1->internal_type(),
                                operand1->index_name(),
                                operand1->index_desc());
      } else if (operand1->expr_op() == ExprOperator::kRef) {
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

  if (!sem_state->bindvar_name()) {
    sem_state->set_bindvar_name(PTBindVar::default_bindvar_name());
  }
  return Status::OK();
}

Status PTRelationExpr::SetupSemStateForOp3(SemState *sem_state) {
  // The states of operand3 is dependent on operand1 in the same way as op2.
  return SetupSemStateForOp2(sem_state);
}

Status PTRelationExpr::AnalyzeOperator(SemContext *sem_context) {
  switch (ql_op_) {
    case QL_OP_EXISTS: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_EXISTS:
      return Status::OK();
    default:
      LOG(FATAL) << "Invalid operator";
  }
  return Status::OK();
}

Status PTRelationExpr::AnalyzeOperator(SemContext *sem_context,
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

Status PTRelationExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2) {
  // "op1" and "op2" must have been analyzed before getting here
  switch (ql_op_) {
    case QL_OP_NOT_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_EQUAL:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(CheckEqualityOperands(sem_context, op1, op2));
      internal_type_ = yb::InternalType::kBoolValue;
      break;
    case QL_OP_LESS_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(CheckInequalityOperands(sem_context, op1, op2));
      internal_type_ = yb::InternalType::kBoolValue;
      break;

    case QL_OP_IN:
    case QL_OP_NOT_IN:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      // For IN operator, we check compatibility between op1's type and op2's value_type.
      if (!sem_context->IsComparable(op1->ql_type_id(), op2->ql_type()->values_type()->main())) {
        return sem_context->Error(this, "Cannot compare values of these datatypes",
                                  ErrorCode::INCOMPARABLE_DATATYPES);
      }
      break;

    case QL_OP_CONTAINS: {
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      // We must check if LHS is collection.
      if (!op1->ql_type()->IsCollection()) {
        return sem_context->Error(this,
            "CONTAINS is only supported for Collections that are not frozen.",
            ErrorCode::DATATYPE_MISMATCH);
      }

      if (op2->is_null()) {
        return sem_context->Error(
            this, "CONTAINS does not support NULL", ErrorCode::INVALID_ARGUMENTS);
      }

      // For CONTAINS operator, we check compatibility between op1's value_type and op2's type.
      auto value_type_lhs = op1->ql_type()->values_type();
      if (!sem_context->IsComparable(value_type_lhs->main(), op2->ql_type_id())) {
        return sem_context->Error(
            this, "Cannot compare values of these datatypes", ErrorCode::INCOMPARABLE_DATATYPES);
      }
      break;
    }

    case QL_OP_CONTAINS_KEY: {
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      // We must check if LHS is map
      if (op1->ql_type()->main() != yb::DataType::MAP) {
        return sem_context->Error(
            this,
            "CONTAINS KEY is only supported for Maps that are not frozen.",
            ErrorCode::DATATYPE_MISMATCH);
      }

      if (op2->is_null()) {
        return sem_context->Error(
            this, "CONTAINS KEY does not support NULL.", ErrorCode::INVALID_ARGUMENTS);
      }

      // For CONTAINS KEY operator, we check compatibility between op1's key type and op2's type.
      auto value_type_lhs = op1->ql_type()->keys_type();
      if (!sem_context->IsComparable(value_type_lhs->main(), op2->ql_type_id())) {
        return sem_context->Error(
            this, "Cannot compare values of these datatypes", ErrorCode::INCOMPARABLE_DATATYPES);
      }
      break;
    }

    default:
      return sem_context->Error(this, "Operator not supported yet",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }

  auto process_token_fn = [this, sem_context, &op2](
      Status *s, const PTBcall *bcall, AnalyzeStepState *state) -> bool {
        if (strcmp(DCHECK_NOTNULL(bcall)->name()->c_str(), "token") == 0 ||
            strcmp(bcall->name()->c_str(), "partition_hash") == 0) {
          const PTToken *token = static_cast<const PTToken *>(bcall);
          DCHECK_ONLY_NOTNULL(s);
          if (token->is_partition_key_ref()) {
            *s = DCHECK_NOTNULL(state)->AnalyzePartitionKeyOp(sem_context, this, op2);
          } else {
            *s = sem_context->Error(this,
                                    "token/partition_hash calls need to reference partition key",
                                    ErrorCode::FEATURE_NOT_SUPPORTED);
          }
          return true;
        }
        return false;
      };

  // Collecting scan information if its state is set.
  SelectScanInfo *scan_state = sem_context->scan_state();
  if (scan_state != nullptr) {
    // Add reference to expressions and operators in WHERE clause for indexing analysis.
    if (scan_state->analyze_where()) {
      if (op1->index_desc()) {
        RETURN_NOT_OK(scan_state->AddWhereExpr(sem_context, this, op1->index_desc(), op2));

      } else if (op1->expr_op() == ExprOperator::kRef) {
        const PTRef *ref = static_cast<const PTRef *>(op1.get());
        RETURN_NOT_OK(scan_state->AddWhereExpr(sem_context, this, ref->desc(), op2));

      } else if (op1->expr_op() == ExprOperator::kSubColRef) {
        const PTSubscriptedColumn *ref = static_cast<const PTSubscriptedColumn *>(op1.get());
        RETURN_NOT_OK(scan_state->AddWhereExpr(sem_context, this, ref->desc(), op2, ref->args()));

      } else if (op1->expr_op() == ExprOperator::kJsonOperatorRef) {
        const PTJsonColumnWithOperators *ref =
          static_cast<const PTJsonColumnWithOperators*>(op1.get());
        RETURN_NOT_OK(scan_state->AddWhereExpr(sem_context, this, ref->desc(), op2,
                                                  ref->operators()));
      } else if (op1->expr_op() == ExprOperator::kBcall) {
        const PTBcall *bcall = static_cast<const PTBcall *>(op1.get());
        Status s_token;
        if (process_token_fn(&s_token, bcall, scan_state)) {
          return s_token;
        }
      }
    }

    // Add filtering expressions in IF clause for indexing analysis.
    if (scan_state->analyze_if()) {
      if (op1->index_desc() ||
          op1->expr_op() == ExprOperator::kRef ||
          op1->expr_op() == ExprOperator::kSubColRef ||
          op1->expr_op() == ExprOperator::kJsonOperatorRef) {
        RETURN_NOT_OK(scan_state->AddFilteringExpr(sem_context, this));
      }
    }
  }

  WhereExprState *where_state = sem_context->where_state();
  if (where_state != nullptr) {
    // CheckLhsExpr already checks that this is either kRef or kBcall
    DCHECK(
        op1->index_desc() != nullptr || op1->expr_op() == ExprOperator::kRef ||
        op1->expr_op() == ExprOperator::kSubColRef ||
        op1->expr_op() == ExprOperator::kJsonOperatorRef ||
        op1->expr_op() == ExprOperator::kBcall || op1->expr_op() == ExprOperator::kCollection);
    if (op1->index_desc()) {
      return where_state->AnalyzeColumnOp(sem_context, this, op1->index_desc(), op2);
    } else if (op1->expr_op() == ExprOperator::kRef) {
      const PTRef *ref = static_cast<const PTRef *>(op1.get());
      return where_state->AnalyzeColumnOp(sem_context, this, ref->desc(), op2);
    } else if (op1->expr_op() == ExprOperator::kSubColRef) {
      const PTSubscriptedColumn *ref = static_cast<const PTSubscriptedColumn *>(op1.get());
      return where_state->AnalyzeColumnOp(sem_context, this, ref->desc(), op2, ref->args());
    } else if (op1->expr_op() == ExprOperator::kJsonOperatorRef) {
      const PTJsonColumnWithOperators *ref =
          static_cast<const PTJsonColumnWithOperators*>(op1.get());

      return where_state->AnalyzeColumnOp(sem_context, this, ref->desc(), op2, ref->operators());
    } else if (op1->expr_op() == ExprOperator::kBcall) {
      const PTBcall *bcall = static_cast<const PTBcall *>(op1.get());
      Status s_token;
      if (process_token_fn(&s_token, bcall, where_state)) {
        return s_token;
      } else if (strcmp(bcall->name()->c_str(), "ttl") == 0 ||
                 strcmp(bcall->name()->c_str(), "writetime") == 0 ||
                 strcmp(bcall->name()->c_str(), "cql_cast") == 0) {
        PTBcall::SharedPtr bcall_shared = std::make_shared<PTBcall>(*bcall);
        return where_state->AnalyzeColumnFunction(sem_context, this, op2, bcall_shared);
      } else {
        return sem_context->Error(loc(), "Builtin call not allowed in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
    } else if (op1->expr_op() == ExprOperator::kCollection) {
      const PTCollectionExpr *collection_expr = static_cast<const PTCollectionExpr *>(op1.get());
      std::vector<const ColumnDesc *> col_descs;
      col_descs.reserve(collection_expr->values().size());
      for (auto &value : collection_expr->values()) {
        PTRef *ref = static_cast<PTRef *>(value.get());
        col_descs.push_back(ref->desc());
      }
      return where_state->AnalyzeMultiColumnOp(sem_context, this, col_descs, op2);
    }
  }

  if (sem_context->idx_predicate_state() != nullptr) {
    DCHECK(op1->index_desc() != nullptr ||
           op1->expr_op() == ExprOperator::kRef ||
           op1->expr_op() == ExprOperator::kSubColRef ||
           op1->expr_op() == ExprOperator::kJsonOperatorRef ||
           op1->expr_op() == ExprOperator::kBcall);
    // TODO(Piyush): Block mutable functions only. Allow all other functions.

    // Allow only expressions involving columns. Block subscripted/json col+operators.
    if (op1->expr_op() != ExprOperator::kRef) {
      return sem_context->Error(this,
        "Partial index where clause only allows operators on table columns",
        ErrorCode::FEATURE_NOT_SUPPORTED);
    }
  }
  return Status::OK();
}

Status PTRelationExpr::AnalyzeOperator(SemContext *sem_context,
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

string PTRelationExpr::QLName(QLNameOption option) const {
  switch (ql_op_) {
    case QL_OP_NOOP:
      return "NO OP";

    // Logic operators that take one operand.
    case QL_OP_NOT:
      return string("NOT ") + op1()->QLName(option);
    case QL_OP_IS_TRUE:
      return op1()->QLName(option) + "IS TRUE";
    case QL_OP_IS_FALSE:
      return op1()->QLName(option) + "IS FALSE";

      // Logic operators that take two or more operands.
    case QL_OP_AND:
      return op1()->QLName(option) + " AND " + op2()->QLName(option);
    case QL_OP_OR:
      return op1()->QLName(option) + " OR " + op2()->QLName(option);

      // Relation operators that take one operand.
    case QL_OP_IS_NULL:
      return op1()->QLName(option) + " IS NULL";
    case QL_OP_IS_NOT_NULL:
      return op1()->QLName(option) + " IS NOT NULL";

      // Relation operators that take two operands.
    case QL_OP_EQUAL:
      return op1()->QLName(option) + " == " + op2()->QLName(option);
    case QL_OP_LESS_THAN:
      return op1()->QLName(option) + " < " + op2()->QLName(option);
    case QL_OP_LESS_THAN_EQUAL:
      return op1()->QLName(option) + " <= " + op2()->QLName(option);
    case QL_OP_GREATER_THAN:
      return op1()->QLName(option) + " > " + op2()->QLName(option);
    case QL_OP_GREATER_THAN_EQUAL:
      return op1()->QLName(option) + " >= " + op2()->QLName(option);
    case QL_OP_NOT_EQUAL:
      return op1()->QLName(option) + " != " + op2()->QLName(option);

    case QL_OP_LIKE:
      return op1()->QLName(option) + " LIKE " + op2()->QLName(option);
    case QL_OP_NOT_LIKE:
      return op1()->QLName(option) + " NOT LIKE " +
          op2()->QLName(option);
    case QL_OP_IN:
      return op1()->QLName(option) + " IN " + op2()->QLName(option);
    case QL_OP_NOT_IN:
      return op1()->QLName(option) + " NOT IN " + op2()->QLName(option);
    case QL_OP_CONTAINS:
      return op1()->QLName(option) + " CONTAINS " + op2()->QLName(option);
    case QL_OP_CONTAINS_KEY:
      return op1()->QLName(option) + " CONTAINS KEY " + op2()->QLName(option);

    // Relation operators that take three operands.
    case QL_OP_BETWEEN:
      return op1()->QLName(option) + " BETWEEN " +
          op2()->QLName(option) + " AND " +
          op3()->QLName(option);
    case QL_OP_NOT_BETWEEN:
      return op1()->QLName(option) + " NOT BETWEEN " +
          op2()->QLName(option) + " AND " +
          op3()->QLName(option);

    // Operators that take no operand. For use in "if" clause only currently.
    case QL_OP_EXISTS:
      return "EXISTS";
    case QL_OP_NOT_EXISTS:
      return "NOT EXISTS";
  }

  return "expr";
}

const ColumnDesc *PTExpr::GetColumnDesc(const SemContext *sem_context) {
  MCString expr_name(MangledName().c_str(), sem_context->PTempMem());
  return GetColumnDesc(sem_context, expr_name, sem_context->current_dml_stmt());
}

const ColumnDesc *PTExpr::GetColumnDesc(const SemContext *sem_context,
                                        const MCString& col_name) const {
  if (sem_context->selecting_from_index()) {
    // Mangle column name when selecting from IndexTable.
    MCString mangled_name(qlexpr::YcqlName::MangleColumnName(col_name.c_str()).c_str(),
                          sem_context->PTempMem());
    return GetColumnDesc(sem_context, mangled_name, sem_context->current_dml_stmt());
  }

  return GetColumnDesc(sem_context, col_name, sem_context->current_dml_stmt());
}

const ColumnDesc *PTExpr::GetColumnDesc(const SemContext *sem_context,
                                        const MCString& desc_name,
                                        PTDmlStmt *stmt) const {
  SelectScanInfo *scan_state = sem_context->scan_state();
  if (scan_state) {
    // Get column from scan_state when analyze index for scanning.
    return scan_state->GetColumnDesc(sem_context, desc_name);
  }

  if (stmt) {
    // Get column from DML statement when compiling a DML statement.
    return stmt->GetColumnDesc(sem_context, desc_name);
  }

  // Get column from symbol table in context.
  return sem_context->GetColumnDesc(desc_name);
}

//--------------------------------------------------------------------------------------------------

Status PTOperatorExpr::SetupSemStateForOp1(SemState *sem_state) {
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

Status PTOperatorExpr::AnalyzeOperator(SemContext *sem_context,
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

std::string PTRef::QLName(QLNameOption option) const {
  if (option == QLNameOption::kMetadataName) {
    // Should only be called after the descriptor is loaded from the catalog by Analyze().
    CHECK(desc_) << "Metadata is not yet loaded to this node";
    return desc_->MetadataName();
  }

  if (option == QLNameOption::kMangledName) {
    return qlexpr::YcqlName::MangleColumnName(name_->QLName());
  }

  return name_->QLName();
}

const MCSharedPtr<MCString>& PTRef::bindvar_name() const {
  return name_->bindvar_name();
}

Status PTRef::AnalyzeOperator(SemContext *sem_context) {
  DCHECK(name_ != nullptr) << "Reference column is not specified";

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  if (!name_->IsSimpleName()) {
    return sem_context->Error(this, "Qualified name not allowed for column reference",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }
  desc_ = GetColumnDesc(sem_context, name_->last_name());
  if (desc_ == nullptr) {
    // If this is a nested select from an uncovered index/partial index,
    // ignore column that is uncovered/only in partial index predicate and not in index cols.
    LOG(INFO) << "Column " << name_->last_name() << " not found";
    return sem_context->IsUncoveredIndexSelect() || sem_context->IsPartialIndexSelect()
        ? Status::OK()
        : sem_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  internal_type_ = desc_->internal_type();
  ql_type_ = desc_->ql_type();
  return Status::OK();
}

Status PTRef::CheckLhsExpr(SemContext *sem_context) {
  // When CQL IF clause is being processed. In that case, disallow reference to primary key columns
  // and counters. No error checking is needed when processing SELECT against INDEX table because
  // we already check it against the UserTable.
  if (sem_context->processing_if_clause() && !sem_context->selecting_from_index()) {
    if (desc_->is_primary() && !sem_context->void_primary_key_condition()) {
      return sem_context->Error(this, "Primary key column reference is not allowed in if clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
    if (desc_->is_counter()) {
      return sem_context->Error(this, "Counter column reference is not allowed in if clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  // Only hash/static columns are supported in the where clause of SELECT DISTINCT.
  if (sem_context->where_state() != nullptr) {
    const PTDmlStmt *dml = sem_context->current_dml_stmt();
    if (dml != nullptr && dml->opcode() == TreeNodeOpcode::kPTSelectStmt &&
        down_cast<const PTSelectStmt*>(dml)->distinct() &&
        !desc_->is_hash() && !desc_->is_static()) {
      return sem_context->Error(this,
                                "Non-partition/static column reference is not supported in the "
                                "where clause of a SELECT DISTINCT statement",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  return Status::OK();
}

void PTRef::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

PTJsonOperator::PTJsonOperator(MemoryContext *memctx,
                               YBLocation::SharedPtr loc,
                               const JsonOperator& json_operator,
                               const PTExpr::SharedPtr& arg)
    : PTExpr(memctx, loc, ExprOperator::kJsonOperatorRef, yb::QLOperator::QL_OP_NOOP,
             InternalType::kJsonbValue, DataType::JSONB),
      json_operator_(json_operator),
      arg_(arg) {
}

PTJsonOperator::~PTJsonOperator() {
}

Status PTJsonOperator::Analyze(SemContext *sem_context) {
  return arg_->Analyze(sem_context);
}

//--------------------------------------------------------------------------------------------------

PTJsonColumnWithOperators::PTJsonColumnWithOperators(MemoryContext *memctx,
                                                     YBLocation::SharedPtr loc,
                                                     const PTQualifiedName::SharedPtr& name,
                                                     const PTExprListNode::SharedPtr& operators)
    : PTOperator0(memctx, loc, ExprOperator::kJsonOperatorRef, yb::QLOperator::QL_OP_NOOP),
      name_(name),
      operators_(operators) {
}

PTJsonColumnWithOperators::~PTJsonColumnWithOperators() {
}

Status PTJsonColumnWithOperators::AnalyzeOperator(SemContext *sem_context) {
  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  desc_ = GetColumnDesc(sem_context, name_->last_name());
  if (desc_ == nullptr) {
    // If this is a nested select from an uncovered index, ignore column that is uncovered.
    return sem_context->IsUncoveredIndexSelect()
        ? Status::OK()
        : sem_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  SemState sem_state(sem_context);

  if (!desc_->ql_type()->IsJson()) {
    return sem_context->Error(this, "Column provided is not json data type",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (operators_->size() == 0) {
    return sem_context->Error(this, "No operators provided.",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Analyze each operator.
  RETURN_NOT_OK(operators_->Analyze(sem_context));

  // Check the last operator to determine type.
  auto json_operator = std::dynamic_pointer_cast<PTJsonOperator>(
      operators_->node_list().back())->json_operator();

  if (json_operator == JsonOperator::JSON_OBJECT) {
    ql_type_ = QLType::Create(DataType::JSONB);
    internal_type_ = InternalType::kJsonbValue;
  } else if (json_operator == JsonOperator::JSON_TEXT) {
    ql_type_ = QLType::Create(DataType::STRING);
    internal_type_ = InternalType::kStringValue;
  } else {
    return sem_context->Error(this, "Invalid operator.",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  return Status::OK();
}

Status PTJsonColumnWithOperators::CheckLhsExpr(SemContext *sem_context) {
  return Status::OK();
}

std::string PTJsonColumnWithOperators::QLName(QLNameOption option) const {
  string qlname;
  if (option == QLNameOption::kMetadataName) {
    DCHECK(desc_) << "Metadata is not yet loaded to this node";
    qlname = desc_->MetadataName();
  } else if (option == QLNameOption::kMangledName) {
    qlname = qlexpr::YcqlName::MangleColumnName(name_->QLName());
  } else {
    qlname = name_->QLName();
  }

  for (PTExpr::SharedPtr expr : operators_->node_list()) {
    qlname += expr->QLName(option);
  }
  return qlname;
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

std::string PTSubscriptedColumn::QLName(QLNameOption option) const {
  std::string ql_name = name_->QLName();
  if (!args_->node_list().empty()) {
    ql_name += "['" + args_->node_list().front()->QLName() + "']";
  }
  return ql_name;
}

Status PTSubscriptedColumn::AnalyzeOperator(SemContext *sem_context) {

  // Check if this refers to the whole table (SELECT *).
  if (name_ == nullptr) {
    return sem_context->Error(this, "Cannot do type resolution for wildcard reference (SELECT *)",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  desc_ = GetColumnDesc(sem_context, name_->last_name());
  if (desc_ == nullptr) {
    // If this is a nested select from an uncovered index, ignore column that is uncovered.
    return sem_context->IsUncoveredIndexSelect()
        ? Status::OK()
        : sem_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  SemState sem_state(sem_context);

  auto curr_ytype = desc_->ql_type();
  auto curr_itype = desc_->internal_type();

  string arg_bindvar_name;
  switch (curr_ytype->main()) {
    case DataType::MAP:
      arg_bindvar_name = PTBindVar::coll_map_key_bindvar_name(desc_->name());
      break;
    case DataType::LIST:
      arg_bindvar_name = PTBindVar::coll_list_index_bindvar_name(desc_->name());
      break;
    default:
      arg_bindvar_name = PTBindVar::default_bindvar_name();
  }

  if (args_ != nullptr) {
    for (const auto &arg : args_->node_list()) {
      if (curr_ytype->keys_type() == nullptr) {
        return sem_context->Error(this, "Columns with elementary types cannot take arguments",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }

      sem_state.SetExprState(curr_ytype->keys_type(),
                             client::YBColumnSchema::ToInternalDataType(curr_ytype->keys_type()));
      sem_state.set_bindvar_name(arg_bindvar_name);
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

const MCSharedPtr<MCString>& PTSubscriptedColumn::bindvar_name() const {
  return name_->bindvar_name();
}

Status PTSubscriptedColumn::CheckLhsExpr(SemContext *sem_context) {
  // If where_state is null, we are processing the IF clause. In that case, disallow reference to
  // primary key columns.
  if (sem_context->where_state() == nullptr &&
      desc_->is_primary() && !sem_context->void_primary_key_condition()) {
    return sem_context->Error(this, "Primary key column reference is not allowed in if expression",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

void PTSubscriptedColumn::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTAllColumns::PTAllColumns(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : PTOperator0(memctx, loc, ExprOperator::kRef, yb::QLOperator::QL_OP_NOOP),
      columns_(memctx) {
}

PTAllColumns::~PTAllColumns() {
}

Status PTAllColumns::AnalyzeOperator(SemContext *sem_context) {
  // Make sure '*' is used only in 'SELECT *' statement.
  const PTDmlStmt *stmt = sem_context->current_dml_stmt();
  if (stmt == nullptr ||
      stmt->opcode() != TreeNodeOpcode::kPTSelectStmt ||
      static_cast<const PTSelectStmt*>(stmt)->selected_exprs().size() > 1) {
    return sem_context->Error(loc(), "Cannot use '*' expression in this context",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  const auto* select_stmt = static_cast<const PTSelectStmt*>(stmt);
  columns_.clear();
  columns_.reserve(select_stmt->column_map().size());
  for (const auto& pair : select_stmt->column_map()) {
    columns_.emplace_back(pair.second);
  }

  // For 'select * ... ' using index only, sort them in the same order as the table columns so that
  // the selected columns are returned in the proper order.
  if (select_stmt->table()->IsIndex()) {
    MCUnorderedMap<int, int> map(sem_context->PTempMem()); // Map of column_id -> indexed_column_id
    for (const auto& column : select_stmt->table()->index_info().columns()) {
      map.emplace(column.column_id, column.indexed_column_id);
    }
    std::sort(columns_.begin(), columns_.end(),
              [&map](const ColumnDesc& a, const ColumnDesc& b) {
                return map[a.id()] < map[b.id()];
              });
  } else {
    std::sort(columns_.begin(), columns_.end(),
              [](const ColumnDesc& a, const ColumnDesc& b) {
                return a.id() < b.id();
              });
  }

  // Note to server that all column are referenced by this statement.
  sem_context->current_dml_stmt()->AddRefForAllColumns();

  // TODO(Mihnea) See if TUPLE datatype can be used here.
  // '*' should be of TUPLE type, but we use the following workaround for now.
  ql_type_ = QLType::Create(DataType::NULL_VALUE_TYPE);
  internal_type_ = InternalType::kListValue;
  return Status::OK();
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

Status PTExprAlias::SetupSemStateForOp1(SemState *sem_state) {
  sem_state->set_allowing_aggregate(sem_state->previous_state()->allowing_aggregate());
  return Status::OK();
}

Status PTExprAlias::AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) {
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
      name_(name) {
}

PTBindVar::PTBindVar(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     PTConstVarInt::SharedPtr user_pos)
    : PTExpr(memctx, loc, ExprOperator::kBindVar),
      user_pos_(user_pos) {
}

PTBindVar::~PTBindVar() {
}

Status PTBindVar::Analyze(SemContext *sem_context) {
  // Before traversing the expression, check if this whole expression is actually a column.
  if (CheckIndexColumn(sem_context)) {
    return Status::OK();
  }

  RETURN_NOT_OK(CheckOperator(sem_context));

  if (name_ == nullptr) {
    name_ = sem_context->bindvar_name();
  }
  alternative_names_ = sem_context->alternative_bindvar_names();

  if (user_pos_ != nullptr) {
    int64_t pos = 0;
    if (!user_pos_->ToInt64(&pos, false).ok()) {
      return sem_context->Error(this, "Bind position is invalid!",
                                ErrorCode::INVALID_ARGUMENTS);
    }

    if (pos <= 0) {
      return sem_context->Error(this, "Bind variable position should be positive!",
                                ErrorCode::INVALID_ARGUMENTS);
    }
    // Convert from 1 based to 0 based.
    set_pos(pos - 1);
  }

  if (sem_context->expr_expected_ql_type()->main() == DataType::UNKNOWN_DATA) {
    // By default bind variables are compatible with any type.
    ql_type_ = QLType::Create(DataType::NULL_VALUE_TYPE);
  } else {
    ql_type_ = sem_context->expr_expected_ql_type();
  }

  internal_type_ = sem_context->expr_expected_internal_type();
  expected_internal_type_ = internal_type_;
  const ColumnDesc* const hash_col_in_context = sem_context->hash_col();
  // If this bindvar is valid in this statement context (table/index)
  // then add it to the binvar list for this statement.
  if (hash_col_in_context != nullptr) {
    hash_col_ = hash_col_in_context; // Store this hash column desc.
    DCHECK(sem_context->current_dml_stmt() != nullptr);
    sem_context->current_dml_stmt()->AddHashColumnBindVar(this);
  }

  return Status::OK();
}

void PTBindVar::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

std::string PTBindVar::bcall_arg_bindvar_name(const std::string& bcall_name, size_t arg_position) {
  return strings::Substitute("arg$0(system.$1)", arg_position, bcall_name);
}

// The name Cassandra uses for binding the collection element keys.
std::string PTBindVar::coll_map_key_bindvar_name(const std::string& col_name) {
  return strings::Substitute("key($0)", col_name);
}

// The name Cassandra uses for binding the list element indexes.
std::string PTBindVar::coll_list_index_bindvar_name(const std::string& col_name) {
  return strings::Substitute("idx($0)", col_name);
}

// The name Cassandra uses for binding the collection element values.
std::string PTBindVar::coll_value_bindvar_name(const std::string& col_name) {
  return strings::Substitute("value($0)", col_name);
}

// The name for binding the JSON attributes.
std::string PTBindVar::json_bindvar_name(const std::string& col_name) {
  return strings::Substitute("json_attr($0)", col_name);
}

}  // namespace ql
}  // namespace yb
