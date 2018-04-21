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

#include "yb/yql/pgsql/ptree/pg_tbcall.h"

#include "yb/client/client.h"

#include "yb/yql/pgsql/ptree/pg_compile_context.h"
#include "yb/util/bfpg/bfpg.h"
#include "yb/common/ql_bfunc.h"

namespace yb {
namespace pgsql {

using std::vector;
using std::shared_ptr;
using std::make_shared;
using strings::Substitute;

using yb::bfpg::BFOpcode;
using yb::bfpg::BFOPCODE_NOOP;
using yb::bfpg::TSOpcode;
using yb::bfpg::BFDecl;

using yb::client::YBColumnSchema;

using BfuncCompile = yb::bfpg::BFCompileApi<PgTExpr, PgTExpr>;

//--------------------------------------------------------------------------------------------------

PgTBcall::PgTBcall(MemoryContext *memctx,
                 PgTLocation::SharedPtr loc,
                 const MCSharedPtr<MCString>& name,
                 PgTExprListNode::SharedPtr args)
  : PgTExpr(memctx, loc, ExprOperator::kBcall, QLOperator::QL_OP_NOOP),
    name_(name),
    args_(args),
    is_server_operator_(false),
    bfopcode_(static_cast<int32_t>(BFOPCODE_NOOP)),
    cast_ops_(memctx),
    result_cast_op_(BFOPCODE_NOOP) {
}

PgTBcall::~PgTBcall() {
}

bool PgTBcall::IsAggregateCall() const {
  return is_server_operator_ && BFDecl::is_aggregate_op(static_cast<TSOpcode>(bfopcode_));
}

CHECKED_STATUS PgTBcall::Analyze(PgCompileContext *compile_context) {
  RETURN_NOT_OK(CheckOperator(compile_context));

  // Analyze arguments of the function call.
  // - Because of function overloading, we cannot determine the expected type before reading the
  //   argument. At this point, the expected type is set to unknown, which is compatible with
  //   all data type.
  // - If the datatype of an argument (such as bind variable) is not determined, and the overloaded
  //   function call cannot be resolved by the rest of the arguments, the parser should raised
  //   error for multiple matches.
  PgSemState sem_state(compile_context, QLType::Create(UNKNOWN_DATA), InternalType::VALUE_NOT_SET);

  int pindex = 0;
  const MCList<PgTExpr::SharedPtr>& exprs = args_->node_list();
  vector<PgTExpr::SharedPtr> params(exprs.size());
  for (const auto& expr : exprs) {
    RETURN_NOT_OK(expr->Analyze(compile_context));
    RETURN_NOT_OK(expr->CheckRhsExpr(compile_context));

    params[pindex] = expr;
    pindex++;
  }

  // Reset the semantics state after analyzing the arguments.
  sem_state.ResetContextState();

  // Type check the builtin call.
  BFOpcode bfopcode;
  const BFDecl *bfdecl;
  PgTExpr::SharedPtr pt_result = PgTConstArg::MakeShared(compile_context->PTempMem(), loc_,
                                                         nullptr);
  Status s = BfuncCompile::FindPgsqlOpcode(name_->c_str(), params, &bfopcode, &bfdecl, pt_result);
  if (!s.ok()) {
    std::string err_msg = string("Failed calling '") + name_->c_str();
    bool got_first_arg = false;
    err_msg += "(";
    for (auto param : params) {
      if (got_first_arg) {
        err_msg += ",";
      }
      err_msg += param->ql_type()->ToString();
      got_first_arg = true;
    }
    err_msg += ")'. ";
    err_msg += s.ToUserMessage();
    LOG(INFO) << err_msg;
    return compile_context->Error(this, err_msg.c_str(), ErrorCode::INVALID_FUNCTION_CALL);
  }

  if (!bfdecl->is_server_operator()) {
    // Use the builtin-function opcode since this is a regular builtin call.
    bfopcode_ = static_cast<int32_t>(bfopcode);

  } else {
    // Use the server opcode since this is a server operator. Ignore the BFOpcode.
    is_server_operator_ = true;
    TSOpcode tsop = bfdecl->tsopcode();
    bfopcode_ = static_cast<int32_t>(tsop);

    // Check error for special cases.
    // TODO(neil) This should be part of the builtin table. Each entry in builtin table should have
    // a function pointer for CheckRequirement().  During analysis, QL will all apply these function
    // pointers to check for any special requirement of an entry.
    if (bfpg::IsAggregateOpcode(tsop)) {
      if (!compile_context->allowing_aggregate()) {
        string errmsg =
          Substitute("Aggregate function $0() cannot be called in this context", name_->c_str());
        return compile_context->Error(loc(), errmsg.c_str(), ErrorCode::INVALID_ARGUMENTS);
      }
      const PgTExpr::SharedPtr& expr = exprs.front();
      if (expr->expr_op() != ExprOperator::kRef &&
          (!expr->IsDummyStar() || tsop != TSOpcode::kCount)) {
        string errmsg = Substitute("Input argument for $0 must be a column", name_->c_str());
        return compile_context->Error(expr->loc(), errmsg.c_str(), ErrorCode::INVALID_ARGUMENTS);
      }
      if (tsop == TSOpcode::kMin || tsop == TSOpcode::kMax) {
        if (pt_result->ql_type()->IsAnyType()) {
          pt_result->set_ql_type(args_->element(0)->ql_type());
        }
      }
    } else if (tsop == TSOpcode::kTtl || tsop == TSOpcode::kWriteTime) {
      const PgTExpr::SharedPtr& expr = exprs.front();
      if (expr->expr_op() != ExprOperator::kRef) {
        string errmsg = Substitute("Input argument for $0 must be a column", name_->c_str());
        return compile_context->Error(expr->loc(), errmsg.c_str(), ErrorCode::INVALID_ARGUMENTS);
      }
      const PgTRef *ref = static_cast<const PgTRef *>(expr.get());
      if (ref->desc()->is_primary()) {
        string errmsg = Substitute("Input argument for $0 cannot be primary key", name_->c_str());
        return compile_context->Error(expr->loc(), errmsg.c_str(), ErrorCode::INVALID_ARGUMENTS);
      }
      if (ref->desc()->ql_type()->IsParametric()) {
        string errmsg = Substitute("Input argument for $0 is of wrong datatype", name_->c_str());
        return compile_context->Error(expr->loc(), errmsg.c_str(), ErrorCode::INVALID_ARGUMENTS);
      }

    } else if (bfdecl->is_collection_bcall()) {
      // Collection operations require special handling during type analysis
      // 1. Casting check is not needed since conversion between collection types is not allowed
      // 2. Additional type inference is needed for the parameter types of the collections
      DCHECK(pt_result->ql_type()->IsParametric());
      cast_ops_.resize(exprs.size(), BFOPCODE_NOOP);

      if (!compile_context->processing_set_clause()) {
        return compile_context->Error(this, "Collection operations only allowed in set clause",
                                  ErrorCode::INVALID_FUNCTION_CALL);
      }

      auto it = exprs.begin();
      auto col_ref = *it;
      auto arg = *(++it);

      // list addition allows column ref to be second argument
      if (strcmp(bfdecl->ql_name(), "+list") == 0 &&
          col_ref->expr_op() != ExprOperator::kRef) {
        arg = col_ref;
        col_ref = *it;
      }

      if (col_ref->expr_op() != ExprOperator::kRef) {
        return compile_context->Error(col_ref.get(),
                                  "Expected reference to column of a collection datatype",
                                  ErrorCode::INVALID_FUNCTION_CALL);
      }

      PgTRef *pt_ref = static_cast<PgTRef *>(col_ref.get());
      if (compile_context->lhs_col() != pt_ref->desc()) {
        return compile_context->Error(
            this,
            "Expected main argument for collection operation to reference the column being set",
            ErrorCode::INVALID_FUNCTION_CALL);
      }

      PgSemState state(compile_context);
      // Argument type is same as type of the referenced column (except for subtracting from MAP)
      auto arg_ytype = col_ref->ql_type();
      auto arg_itype = col_ref->internal_type();
      // Subtraction from MAP takes keys to be removed as param so arg type for MAP<A,B> is SET<A>
      if (strcmp(bfdecl->ql_name(), "map-") == 0) {
        arg_ytype = QLType::CreateTypeSet(col_ref->ql_type()->param_type(0));
        arg_itype = InternalType::kSetValue;
      }
      state.SetExprState(arg_ytype, arg_itype);
      RETURN_NOT_OK(arg->Analyze(compile_context));

      // Type resolution (for map/set): (cref + <expr>) should have same type as (cref).
      ql_type_ = col_ref->ql_type();
      internal_type_ = arg_itype;

      // return type is same as type of the referenced column
      state.SetExprState(col_ref->ql_type(), col_ref->internal_type());
      RETURN_NOT_OK(CheckExpectedTypeCompatibility(compile_context));

      // For "UPDATE ... SET list = list - x ..." , the list needs to be read first in order to
      // subtract (remove) elements from it.
      if (strcmp(bfdecl->ql_name(), "list-") == 0) {
        compile_context->current_dml_stmt()->AddColumnRef(*pt_ref->desc());
      }

      return Status::OK();
    }
  }

  // Find the cast operator if arguments' ql_type_id are not an exact match with signature.
  pindex = 0;
  cast_ops_.resize(exprs.size(), BFOPCODE_NOOP);
  const std::vector<DataType> &formal_types = bfdecl->param_types();
  for (const auto &expr : exprs) {
    if (formal_types[pindex] == DataType::TYPEARGS) {
      // For variadic functions, accept all arguments without casting.
      break;
    }

    // Converting or casting arguments to expected type for the function call.
    // - If argument and formal datatypes are the same, no conversion is needed. It's a NOOP.
    // - Currently, we only allowed constant expressions which would be folded to the correct type
    //   by the QL engine before creating protobuf messages.
    // - When we support more complex expressions, CAST would be used for the conversion. It'd be
    //   a bug if casting failed.
    if (expr->expr_op() == ExprOperator::kConst) {
      // Check if constant folding is possible between the two datatype.
      PgSemState arg_state(compile_context,
                         QLType::Create(formal_types[pindex]),
                         YBColumnSchema::ToInternalDataType(QLType::Create(formal_types[pindex])));

      RETURN_NOT_OK(expr->Analyze(compile_context));

    } else if (expr->ql_type_id() != formal_types[pindex]) {
      // Future usage: Need to cast from argument type to formal type.
      s = BfuncCompile::FindCastOpcode(expr->ql_type_id(), formal_types[pindex],
          &cast_ops_[pindex]);
      if (!s.ok()) {
        LOG(ERROR) << "Arguments to builtin call " << name_->c_str() << "() is compatible with "
                   << "its signature but converting the argument to the desired type failed";
        string err_msg = (s.ToUserMessage() + "CAST " + expr->ql_type()->ToString() + " AS " +
            QLType::ToCQLString(formal_types[pindex]) + " failed");
        return compile_context->Error(this, err_msg.c_str(), ErrorCode::INVALID_FUNCTION_CALL);
      }
    }
    pindex++;
  }

  // TODO(neil) Not just BCALL, but each expression should have a "result_cast_op_". At execution
  // time if the cast is present, the result of the expression must be casted to the correct type.
  // Find the correct casting op for the result if expected type is not the same as return type.
  if (!compile_context->expr_expected_ql_type()->IsUnknown()) {
    s = BfuncCompile::FindCastOpcode(pt_result->ql_type()->main(),
        compile_context->expr_expected_ql_type()->main(),
        &result_cast_op_);
    ql_type_ = compile_context->expr_expected_ql_type();
  } else {
    ql_type_ = pt_result->ql_type();
  }

  internal_type_ = yb::client::YBColumnSchema::ToInternalDataType(ql_type_);
  return CheckExpectedTypeCompatibility(compile_context);
}

CHECKED_STATUS PgTBcall::CheckOperator(PgCompileContext *compile_context) {
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
