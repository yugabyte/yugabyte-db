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

PTBfunc::PTBfunc(MemoryContext *memctx,
                 YBLocation::SharedPtr loc,
                 const MCString::SharedPtr& name,
                 PTExprListNode::SharedPtr args)
  : PTExpr(memctx, loc, ExprOperator::kBfunc),
    name_(name),
    args_(args),
    opcode_(yb::bfyql::OPCODE_NOOP),
    cast_ops_(memctx),
    result_(nullptr) {
}

PTBfunc::~PTBfunc() {
}

CHECKED_STATUS PTBfunc::Analyze(SemContext *sem_context) {
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
  Status s = BfuncCompile::FindYqlOpcode(name_->c_str(), params, &opcode_, &bfdecl, pt_result);
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
  yql_type_id_ = pt_result->yql_type_id();
  internal_type_ = yb::client::YBColumnSchema::ToInternalDataType(YQLType(yql_type_id_));

  //------------------------------------------------------------------------------------------------
  // The semantic analysis phase should be ended here at this line.
  //------------------------------------------------------------------------------------------------

  //------------------------------------------------------------------------------------------------
  // Experiment Only: Evaluating binary '+' operators and fold it into constant.
  // Builtin calls should have be implemented with the following steps.
  // - During compilation, we seeks the opcodes and checks for semantic errors.
  //   This step is completed.
  // - During execution, the executor constructs an expression tree of YQLValues.
  // - DocDB would execute the YQLValue expression tree using YQLBfunc API.
  //
  // The following code section is to demonstrate how YQLBfunc API is used to execute a builtin
  // call node of the YQLValue expression tree. In the near the future, we'll need to construct
  // an expression tree of YQLValues and execute it. This code does not show how that tree is
  // constructed. It only shows how a builtin call node of that tree is executed.
  //
  // - For each builtin call node, convert its arguments from treenode to YQLValue.
  // - If cast operation is needed for an argument, execute it.
  // - Execute the builtin calls that take vector<YQLValue> as arguments.
  if (!FLAGS_yql_experiment_support_expression) {
    return Status::OK();
  }
  CHECK(strcmp(name_->c_str(), "+") == 0) << "Acceptting only '+' operator for the experiment.";
  CHECK(params.size() == 2) << "Expecting exactly 2 parameters";

  // Convert treenodes to YQLValue. Use the cast operator if needed.
  vector<std::shared_ptr<YQLValue>> yql_values(exprs.size());
  pindex = 0;
  for (const auto& expr : exprs) {
    yql_values[pindex] = expr->ToYqlValue(cast_ops_[pindex]);
    pindex++;
  }

  // Execute builtin function call.
  shared_ptr<YQLValue> result = make_shared<YQLValueWithPB>();
  RETURN_NOT_OK(YQLBfunc::Exec(opcode_, yql_values, result));

  // Save the result to this node.
  // Since this code is going to be deleted, just reallocate result for every execution.
  switch(result->type()) {
    case InternalType::VALUE_NOT_SET:
      result_ = nullptr;
      break;
    case InternalType::kInt8Value:
      result_ = PTConstInt::MakeShared(sem_context->PTreeMem(), loc_, result->int8_value());
      break;
    case InternalType::kInt16Value:
      result_ = PTConstInt::MakeShared(sem_context->PTreeMem(), loc_, result->int16_value());
      break;
    case InternalType::kInt32Value:
      result_ = PTConstInt::MakeShared(sem_context->PTreeMem(), loc_, result->int32_value());
      break;
    case InternalType::kInt64Value:
      result_ = PTConstInt::MakeShared(sem_context->PTreeMem(), loc_, result->int64_value());
      break;
    case InternalType::kFloatValue:
      result_ = PTConstDouble::MakeShared(sem_context->PTreeMem(), loc_, result->float_value());
      break;
    case InternalType::kDoubleValue:
      result_ = PTConstDouble::MakeShared(sem_context->PTreeMem(), loc_, result->double_value());
      break;
    case InternalType::kStringValue: {
      MCString::SharedPtr mc_string = MCString::MakeShared(sem_context->PTreeMem(),
                                                           result->string_value().c_str());
      result_ = PTConstText::MakeShared(sem_context->PTreeMem(), loc_, mc_string);
      break;
    }

    default:
      LOG(FATAL) << "Not supported";
  }

  // Change the internal_type_ to match the result_ datatype.
  yql_type_id_ = result_->yql_type_id();
  internal_type_ = result_->internal_type();

  // Set the result.
  return Status::OK();
}

Status PTBfunc::FindCastOpcode(DataType source, DataType target, BFOpcode *opcode) {
  // Find conversion opcode.
  const BFDecl *found_decl = nullptr;
  std::vector<DataType> actual_types = { source, target };
  DataType return_type = DataType::UNKNOWN_DATA;
  return BfuncCompile::FindYqlOpcode(bfyql::kCastFuncName, actual_types, opcode, &found_decl,
                                     &return_type);
}

shared_ptr<YQLValue> PTBfunc::ToYqlValue(BFOpcode cast_opcode) {
  if (result_ == nullptr) {
    shared_ptr<YQLValue> yql_value = make_shared<YQLValueWithPB>();
    yql_value->SetNull();
    return yql_value;
  }

  return result_->ToYqlValue(cast_opcode);
}

// Experimental: Convert current treenode to YQLValue.
shared_ptr<YQLValue> PTExpr::ToYqlValue(BFOpcode cast_opcode) {
  if (op_ != ExprOperator::kConst) {
    LOG(FATAL) << "Not supported";
    return nullptr;
  }

  shared_ptr<YQLValue> yql_value = std::make_shared<YQLValueWithPB>();
  switch (internal_type_) {
    case InternalType::VALUE_NOT_SET:
      yql_value->SetNull();
      break;
    case InternalType::kInt64Value:
      yql_value->set_int64_value(static_cast<const PTConstInt*>(this)->Eval());
      break;
    case InternalType::kDoubleValue:
      yql_value->set_double_value(static_cast<const PTConstDouble*>(this)->Eval());
      break;
    case InternalType::kStringValue:
      yql_value->set_string_value(static_cast<const PTConstText*>(this)->Eval()->c_str());
      break;
    default:
      LOG(FATAL) << "Not supported";
  }

  if (cast_opcode == yb::bfyql::OPCODE_NOOP) {
    return yql_value;
  }

  // Need to cast the datatype to expected type.
  shared_ptr<YQLValue> converted_value = make_shared<YQLValueWithPB>();
  vector<std::shared_ptr<YQLValue>> yql_values = { yql_value, converted_value };
  CHECK_OK(YQLBfunc::Exec(cast_opcode, yql_values, nullptr));

  return converted_value;
}

}  // namespace sql
}  // namespace yb
