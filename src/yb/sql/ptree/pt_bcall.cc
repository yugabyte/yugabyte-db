//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_bcall.h"
#include "yb/sql/ptree/sem_context.h"
#include "yb/util/bfyql/bfyql.h"
#include "yb/common/yql_bfunc.h"

namespace yb {
namespace sql {

using std::vector;
using std::shared_ptr;
using std::make_shared;

using yb::bfyql::BFOpcode;
using yb::bfyql::BFDecl;
using yb::client::YBColumnSchema;

using BfuncCompile = yb::bfyql::BFCompileApi<PTExpr, PTExpr>;

//--------------------------------------------------------------------------------------------------

PTBcall::PTBcall(MemoryContext *memctx,
                 YBLocation::SharedPtr loc,
                 const MCSharedPtr<MCString>& name,
                 PTExprListNode::SharedPtr args)
  : PTExpr(memctx, loc, ExprOperator::kBcall, YQLOperator::YQL_OP_NOOP),
    name_(name),
    args_(args),
    bf_opcode_(yb::bfyql::OPCODE_NOOP),
    cast_ops_(memctx),
    result_cast_op_(yb::bfyql::OPCODE_NOOP) {
}

PTBcall::~PTBcall() {
}

CHECKED_STATUS PTBcall::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(CheckOperator(sem_context));

  // Analyze arguments of the function call.
  // - Because of function overloading, we cannot determine the expected type before reading the
  //   argument. At this point, the expected type is set to unknown, which is compatible with
  //   all data type.
  // - If the datatype of an argument (such as bind variable) is not determined, and the overloaded
  //   function call cannot be resolved by the rest of the arguments, the parser should raised
  //   error for multiple matches.
  SemState sem_state(sem_context);
  sem_state.SetExprState(YQLType::Create(UNKNOWN_DATA), InternalType::VALUE_NOT_SET);

  int pindex = 0;
  const MCList<PTExpr::SharedPtr>& exprs = args_->node_list();
  vector<PTExpr::SharedPtr> params(exprs.size());
  for (const auto& expr : exprs) {
    RETURN_NOT_OK(expr->Analyze(sem_context));
    RETURN_NOT_OK(expr->CheckRhsExpr(sem_context));

    params[pindex] = expr;
    pindex++;
  }

  // Reset the semantics state after analyzing the arguments.
  sem_state.ResetContextState();

  // Type check the builtin call.
  const BFDecl *bfdecl;
  PTExpr::SharedPtr pt_result = PTConstArg::MakeShared(sem_context->PTempMem(), loc_, nullptr);
  Status s = BfuncCompile::FindYqlOpcode(name_->c_str(), params, &bf_opcode_, &bfdecl, pt_result);
  if (!s.ok()) {
    std::string err_msg = string("Failed to process builtin call: ") + name_->c_str();
    bool first_arg = true;
    for (auto param : params) {
      err_msg += string(first_arg ? "(" : ",");
      err_msg += param->yql_type()->ToString();
      first_arg = false;
    }
    err_msg += ")";
    err_msg = s.ToString() + ". " + err_msg;
    LOG(INFO) << s.ToString() << ". " << err_msg;
    return sem_context->Error(loc(), err_msg.c_str(), ErrorCode::INVALID_FUNCTION_CALL);
  }

  // Collection operations require special handling during type analysis
  // 1. Casting check is not needed since type conversion between collection types is not allowed
  // 2. Additional type inference is needed for the parameter types of the collections
  if (pt_result->yql_type()->IsParametric()) {
    cast_ops_.resize(exprs.size(), yb::bfyql::OPCODE_NOOP);

    if (strcmp(bfdecl->cpp_name(), "AddMapMap") == 0 ||
        strcmp(bfdecl->cpp_name(), "AddMapSet") == 0 ||
        strcmp(bfdecl->cpp_name(), "AddSetSet") == 0 ||
        strcmp(bfdecl->cpp_name(), "AddListList") == 0 ||
        strcmp(bfdecl->cpp_name(), "SubMapSet") == 0 ||
        strcmp(bfdecl->cpp_name(), "SubSetSet") == 0 ||
        strcmp(bfdecl->cpp_name(), "SubListList") == 0) {

      if (!sem_context->processing_set_clause()) {
        return sem_context->Error(loc(),
            "Collection operations only allowed in set clause",
            ErrorCode::INVALID_FUNCTION_CALL);
      }

      auto it = exprs.begin();
      auto col_ref = *it;
      auto arg = *(++it);

      // list addition allows column ref to be second argument
      if (strcmp(bfdecl->cpp_name(), "AddListList") == 0 &&
          col_ref->expr_op() != ExprOperator::kRef) {
        arg = col_ref;
        col_ref = *it;
      }

      if (col_ref->expr_op() != ExprOperator::kRef) {
        return sem_context->Error(loc(),
            "Expected main argument for collection operation to be column reference",
            ErrorCode::INVALID_FUNCTION_CALL);
      }

      PTRef *pt_ref = static_cast<PTRef *>(col_ref.get());

      if (sem_context->lhs_col() != pt_ref->desc()) {
        return sem_context->Error(loc(),
            "Expected main argument for collection operation to reference the column being set",
            ErrorCode::INVALID_FUNCTION_CALL);
      }

      if (arg->expr_op() != ExprOperator::kCollection) {
        return sem_context->Error(loc(),
            "Expected auxiliary argument for collection operations to be collection literal",
            ErrorCode::INVALID_FUNCTION_CALL);
      }

      SemState state(sem_context);
      // Argument type is same as type of the referenced column (except for subtracting from MAP)
      auto arg_ytype = col_ref->yql_type();
      auto arg_itype = col_ref->internal_type();
      // Subtraction from MAP takes keys to be removed as param so arg type for MAP<A,B> is SET<A>
      if (strcmp(bfdecl->cpp_name(), "SubMapSet") == 0) {
        arg_ytype = YQLType::CreateTypeSet(col_ref->yql_type()->param_type(0));
        arg_itype = InternalType::kSetValue;
      }
      state.SetExprState(arg_ytype, arg_itype);
      RETURN_NOT_OK(arg->Analyze(sem_context));

      // Type resolution (for map/set): (cref + <expr>) should have same type as (cref).
      yql_type_ = col_ref->yql_type();
      internal_type_ = arg_itype;

      // return type is same as type of the referenced column
      state.SetExprState(col_ref->yql_type(), col_ref->internal_type());
      return CheckExpectedTypeCompatibility(sem_context);
    }

    // default
    return sem_context->Error(loc(),
                              "Unsupported builtin call for collections",
                              ErrorCode::INVALID_FUNCTION_CALL);

  } else {
    // Find the cast operator if arguments' yql_type_id are not an exact match with signature.
    pindex = 0;
    cast_ops_.resize(exprs.size(), yb::bfyql::OPCODE_NOOP);
    const std::vector<DataType> &formal_types = bfdecl->param_types();
    for (const auto &expr : exprs) {
      if (formal_types[pindex] == DataType::TYPEARGS) {
        // For variadic functions, accept all arguments without casting.
        break;
      }

      // Converting or casting arguments to expected type for the function call.
      // - If argument and formal datatypes are the same, no conversion is needed. It's a NOOP.
      // - Currently, we only allowed constant expressions which would be folded to the correct type
      //   by the YQL engine before creating protobuf messages.
      // - When we support more complex expressions, CAST would be used for the conversion. It'd be
      //   a bug if casting failed.
      if (expr->expr_op() == ExprOperator::kConst) {
        // Check if constant folding is possible between the two datatype.
        SemState arg_state(sem_context);
        arg_state.SetExprState(
            YQLType::Create(formal_types[pindex]),
            YBColumnSchema::ToInternalDataType(YQLType::Create(formal_types[pindex])));
            RETURN_NOT_OK(expr->Analyze(sem_context));

      } else if (expr->yql_type_id() != formal_types[pindex]) {
        // Future usage: Need to cast from argument type to formal type.
        s = BfuncCompile::FindCastOpcode(expr->yql_type_id(), formal_types[pindex],
            &cast_ops_[pindex]);
        if (!s.ok()) {
          LOG(ERROR) << "Arguments to builtin call " << name_->c_str() << "() is compatible with "
                     << "its signature but converting the argument to the desired type failed";
          string err_msg = (s.ToString() + "CAST " + expr->yql_type()->ToString() + " AS " +
              YQLType::ToCQLString(formal_types[pindex]) + " failed");
          return sem_context->Error(loc(), err_msg.c_str(), ErrorCode::INVALID_FUNCTION_CALL);
        }
      }
      pindex++;
    }

    // TODO(neil) Not just BCALL, but each expression should have a "result_cast_op_". At execution
    // time if the cast is present, the result of the expression must be casted to the correct type.

    // Find the correct casting op for the result if expected type is not the same as return type.
    if (!sem_context->expr_expected_yql_type()->IsUnknown()) {
      s = BfuncCompile::FindCastOpcode(pt_result->yql_type()->main(),
          sem_context->expr_expected_yql_type()->main(),
          &result_cast_op_);
      yql_type_ = sem_context->expr_expected_yql_type();
    } else {
      yql_type_ = pt_result->yql_type();
    }

    internal_type_ = yb::client::YBColumnSchema::ToInternalDataType(yql_type_);
    return CheckExpectedTypeCompatibility(sem_context);
  }
}

CHECKED_STATUS PTBcall::CheckOperator(SemContext *sem_context) {
  if (sem_context->processing_set_clause() && sem_context->lhs_col()->is_counter()) {
    // Only increment ("+") and decrement ("-") operators are allowed for counters.
    if (strcmp(name_->c_str(), "+") == 0 || strcmp(name_->c_str(), "-") == 0) {
      name_->append("counter");
    } else if (strcmp(name_->c_str(), "+counter") != 0 && strcmp(name_->c_str(), "-counter") != 0) {
      return sem_context->Error(loc(), ErrorCode::INVALID_COUNTING_EXPR);
    }
  }

  /*
  // The following error report on builtin call is to disallow function call for non-COUNTER
  // datatypes. This code block should be enabled if we decide to do so.
  else {
    return sem_context->Error(loc(), "This operator is only allowed for COUNTER in SET clause",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  */

  return Status::OK();
}

CHECKED_STATUS PTBcall::CheckCounterUpdateSupport(SemContext *sem_context) const {
  PTExpr::SharedPtr arg1 = args_->element(0);
  if (arg1->expr_op() != ExprOperator::kRef) {
    return sem_context->Error(arg1->loc(),
                              "Right and left arguments must reference the same counter",
                              ErrorCode::INVALID_COUNTING_EXPR);
  }

  const PTRef *ref = static_cast<const PTRef*>(arg1.get());
  if (ref->desc() != sem_context->lhs_col()) {
    return sem_context->Error(arg1->loc(),
                              "Right and left arguments must reference the same counter",
                              ErrorCode::INVALID_COUNTING_EXPR);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

// Collection constants.
CHECKED_STATUS PTToken::Analyze(SemContext *sem_context) {
  if (sem_context->sem_state()->where_state() == nullptr) {
    return sem_context->Error(loc(), "Token builtin call currently only allowed in where clause",
        ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  RETURN_NOT_OK(PTBcall::Analyze(sem_context));
  RETURN_NOT_OK(CheckOperator(sem_context));

  return Status::OK();
}

CHECKED_STATUS PTToken::CheckOperator(SemContext *sem_context) {
  size_t size = sem_context->current_table()->schema().num_hash_key_columns();
  is_partition_key_ref_ = true;
  if (args().size() != size) {
    is_partition_key_ref_ = false;
  } else {
    int index = 0;
    for (const PTExpr::SharedPtr &arg : args()) {
      if (arg->expr_op() != ExprOperator::kRef) {
        is_partition_key_ref_ = false;
        break;
      }
      PTRef *col_ref = static_cast<PTRef *>(arg.get());
      RETURN_NOT_OK(col_ref->Analyze(sem_context));
      if (col_ref->desc()->index() != index) {
        is_partition_key_ref_ = false;
        break;
      }
      index++;
    }
  }

  // TODO (mihnea) add support for this later
  if (!is_partition_key_ref_) {
    return sem_context->Error(loc(),
        "Token call only allowed as reference to partition key as virtual column",
        ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
