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
// Tree node definitions for expression.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TBCALL_H_
#define YB_YQL_PGSQL_PTREE_PG_TBCALL_H_

#include "yb/yql/pgsql/ptree/pg_texpr.h"

namespace yb {
namespace pgsql {

// Expression node that represents builtin function calls.
class PgTBcall : public PgTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTBcall> SharedPtr;
  typedef MCSharedPtr<const PgTBcall> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTBcall(MemoryContext *memctx,
          PgTLocation::SharedPtr loc,
          const MCSharedPtr<MCString>& name,
          PgTExprListNode::SharedPtr args);
  virtual ~PgTBcall();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PgTBcall::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTBcall>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Access API for arguments.
  const MCList<PgTExpr::SharedPtr>& args() const {
    return args_->node_list();
  }

  // Access API for opcode.
  bool is_server_operator() const {
    return is_server_operator_;
  }
  int32_t bfopcode() const {
    return bfopcode_;
  }

  // Access API for cast opcodes.
  const MCVector<yb::bfpg::BFOpcode>& cast_ops() const {
    return cast_ops_;
  }

  yb::bfpg::BFOpcode result_cast_op() const {
    return result_cast_op_;
  }

  const MCSharedPtr<MCString>& name() const {
    return name_;
  }

  virtual CHECKED_STATUS CheckOperator(PgCompileContext *compile_context) override;

  virtual string QLName() const override {
    string arg_names;
    for (auto arg : args_->node_list()) {
      if (!arg_names.empty()) {
        arg_names += ", ";
      }
      arg_names += arg->QLName();
    }
    return strings::Substitute("$0$1$2$3", name_->c_str(), "(", arg_names, ")");
  }

  virtual bool IsAggregateCall() const override;
  virtual yb::bfpg::TSOpcode aggregate_opcode() const override {
    return is_server_operator_ ? static_cast<yb::bfpg::TSOpcode>(bfopcode_)
                               : yb::bfpg::TSOpcode::kNoOp;
  }

 private:
  // Builtin function name.
  MCSharedPtr<MCString> name_;

  // Arguments to builtin call.
  PgTExprListNode::SharedPtr args_;

  // Builtin opcode can be either "bfpg::BFOpcode" or "bfpg::TSOpcode".
  // If is_tablet_server_operator_ is true, it is a TSOpcode. Otherwise, it is a BFOpcode.
  bool is_server_operator_;
  int32_t bfopcode_;

  // Casting arguments to correct datatype before calling the builtin-function.
  MCVector<yb::bfpg::BFOpcode> cast_ops_;

  // Casting the returned result to expected type is also needed.
  yb::bfpg::BFOpcode result_cast_op_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TBCALL_H_
