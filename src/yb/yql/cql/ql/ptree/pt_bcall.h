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

#pragma once

#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/bfql/gen_opcodes.h"

namespace yb {
namespace ql {

// Expression node that represents builtin function calls.
class PTBcall : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTBcall> SharedPtr;
  typedef MCSharedPtr<const PTBcall> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTBcall(MemoryContext *memctx,
          YBLocationPtr loc,
          const MCSharedPtr<MCString>& name,
          PTExprListNode::SharedPtr args);
  virtual ~PTBcall();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTBcall::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTBcall>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  // Access API for arguments.
  const MCList<PTExprPtr>& args() const {
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
  const MCVector<yb::bfql::BFOpcode>& cast_ops() const {
    return cast_ops_;
  }

  yb::bfql::BFOpcode result_cast_op() const {
    return result_cast_op_;
  }

  const MCSharedPtr<MCString>& name() const {
    return name_;
  }

  // BCall result set column type in QL format.
  void rscol_type_PB(QLTypePB *pb_type) const override;

  virtual Status CheckOperator(SemContext *sem_context) override;

  virtual Status CheckCounterUpdateSupport(SemContext *sem_context) const override;

  Status CheckOperatorAfterArgAnalyze(SemContext *sem_context);

  void CollectReferencedIndexColnames(MCSet<std::string> *col_names) const override;

  std::string QLName(
      qlexpr::QLNameOption option = qlexpr::QLNameOption::kUserOriginalName) const override;
  bool IsAggregateCall() const override;
  yb::bfql::TSOpcode aggregate_opcode() const override;

  virtual bool HaveColumnRef() const override;

 private:
  // Builtin function name.
  MCSharedPtr<MCString> name_;

  // Arguments to builtin call.
  PTExprListNode::SharedPtr args_;

  // Builtin opcode can be either "bfql::BFOpcode" or "bfql::TSOpcode".
  // If is_tablet_server_operator_ is true, it is a TSOpcode. Otherwise, it is a BFOpcode.
  bool is_server_operator_;
  int32_t bfopcode_;

  // Casting arguments to correct datatype before calling the builtin-function.
  MCVector<yb::bfql::BFOpcode> cast_ops_;

  // Casting the returned result to expected type is also needed.
  yb::bfql::BFOpcode result_cast_op_;
};

class PTToken : public PTBcall {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTToken> SharedPtr;
  typedef MCSharedPtr<const PTToken> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTToken(MemoryContext *memctx,
          YBLocationPtr loc,
          const MCSharedPtr<MCString>& name,
          PTExprListNode::SharedPtr args) : PTBcall(memctx, loc, name, args) { }

  virtual ~PTToken() { }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTToken::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTToken>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  // Check if token call is well formed before analyzing it
  virtual Status CheckOperator(SemContext *sem_context) override;

  bool is_partition_key_ref() const {
    return is_partition_key_ref_;
  }

  virtual const std::string func_name() {
    return "token";
  }

 private:
  // true if this token call is just reference to the partition key, e.g.: "token(h1, h2, h3)"
  // false for regular builtin calls to be evaluated, e.g.: "token(2,3,4)"
  bool is_partition_key_ref_ = false;
};

// Represents partition_hash() function.
class PTPartitionHash : public PTToken {
 public:

  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPartitionHash> SharedPtr;
  typedef MCSharedPtr<const PTPartitionHash> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTPartitionHash(MemoryContext *memctx,
                  YBLocationPtr loc,
                  const MCSharedPtr<MCString>& name,
                  PTExprListNode::SharedPtr args) : PTToken(memctx, loc, name, args) { }

  virtual ~PTPartitionHash() {}

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTPartitionHash::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPartitionHash>(memctx, std::forward<TypeArgs>(args)...);
  }

  const std::string func_name() override {
    return "partition_hash";
  }
};


}  // namespace ql
}  // namespace yb
