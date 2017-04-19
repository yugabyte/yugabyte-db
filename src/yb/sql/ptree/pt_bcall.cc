//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/sem_context.h"
#include "yb/util/bfyql/bfyql.h"
#include "yb/common/yql_bfunc.h"

DEFINE_bool(yql_experiment_support_expression, false, "Whether or not expression is supported");

namespace yb {
namespace sql {

using std::vector;
using std::shared_ptr;
using std::make_shared;

using yb::bfyql::BFOpcode;
using yb::bfyql::BFDecl;
using BfuncCompile = yb::bfyql::BFCompileApi<PTExpr, PTExpr>;

//--------------------------------------------------------------------------------------------------

PTBcall::PTBcall(MemoryContext *memctx,
                 YBLocation::SharedPtr loc,
                 const MCString::SharedPtr& name,
                 PTExprListNode::SharedPtr args)
  : PTExpr(memctx, loc, ExprOperator::kBcall, YQLOperator::YQL_OP_NOOP),
    name_(name),
    args_(args),
    bf_opcode_(yb::bfyql::OPCODE_NOOP),
    cast_ops_(memctx) {
}

PTBcall::~PTBcall() {
}

CHECKED_STATUS PTBcall::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(CheckOperator(sem_context));

  // Process arguments.
  int pindex = 0;
  const MCList<PTExpr::SharedPtr>& exprs = args_->node_list();
  vector<PTExpr::SharedPtr> params(exprs.size());
  for (const auto& expr : exprs) {
    RETURN_NOT_OK(expr->Analyze(sem_context));
    RETURN_NOT_OK(expr->CheckRhsExpr(sem_context));

    params[pindex] = expr;
    pindex++;
  }

  // Type check the builtin call.
  const BFDecl *bfdecl;
  PTExpr::SharedPtr pt_result = PTConstArg::MakeShared(sem_context->PTempMem(), loc_, nullptr);
  Status s = BfuncCompile::FindYqlOpcode(name_->c_str(), params, &bf_opcode_, &bfdecl, pt_result);
  if (!s.ok()) {
    return sem_context->Error(loc(), s.ToString().c_str(), ErrorCode::INVALID_FUNCTION_CALL);
  }

  // Find the cast operator if arguments' yql_type_id are not an exact match with signature.
  pindex = 0;
  cast_ops_.resize(exprs.size(), yb::bfyql::OPCODE_NOOP);
  const std::vector<DataType>& formal_types = bfdecl->param_types();
  for (const auto& expr : exprs) {
    if (formal_types[pindex] == DataType::TYPEARGS) {
      // For variadic functions, accept all arguments without casting.
      break;
    }

    if (expr->yql_type_id() == formal_types[pindex]) {
      cast_ops_[pindex] = yb::bfyql::OPCODE_NOOP;
    } else {
      s = FindCastOpcode(expr->yql_type_id(), formal_types[pindex], &cast_ops_[pindex]);
      if (!s.ok()) {
        LOG(ERROR) << "Arguments to builtin call " << name_->c_str() << "() is compatible with "
                   << "its signature but converting the argument to the desired type failed";
        return sem_context->Error(loc(), s.ToString().c_str(), ErrorCode::INVALID_FUNCTION_CALL);
      }
    }
    pindex++;
  }

  // Initialize this node datatype.
  yql_type_ = pt_result->yql_type();
  internal_type_ = yb::client::YBColumnSchema::ToInternalDataType(yql_type_);
  return Status::OK();
}

Status PTBcall::FindCastOpcode(DataType source, DataType target, BFOpcode *opcode) {
  // Find conversion opcode.
  const BFDecl *found_decl = nullptr;
  std::vector<DataType> actual_types = { source, target };
  DataType return_type = DataType::UNKNOWN_DATA;
  return BfuncCompile::FindYqlOpcode(bfyql::kCastFuncName, actual_types, opcode, &found_decl,
                                     &return_type);
}

}  // namespace sql
}  // namespace yb
