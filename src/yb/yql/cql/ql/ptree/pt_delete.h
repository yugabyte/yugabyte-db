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
// Tree node definitions for DELETE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class PTDeleteStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDeleteStmt> SharedPtr;
  typedef MCSharedPtr<const PTDeleteStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTDeleteStmt(MemoryContext *memctx,
               YBLocationPtr loc,
               PTExprListNode::SharedPtr target,
               PTTableRef::SharedPtr relation,
               PTDmlUsingClausePtr using_clause = nullptr,
               PTExprPtr where_clause = nullptr,
               PTExprPtr if_clause = nullptr,
               bool else_error = false,
               bool returns_status = false);
  virtual ~PTDeleteStmt();

  template<typename... TypeArgs>
  inline static PTDeleteStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTDeleteStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  ExplainPlanPB AnalysisResultToPB() override;

  // Table name.
  client::YBTableName table_name() const override {
    return relation_->table_name();
  }

  // Returns location of table name.
  const YBLocation& table_loc() const override {
    return relation_->loc();
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTDeleteStmt;
  }

  Status AnalyzeTarget(TreeNode *target, SemContext *sem_context);

  bool IsWriteOp() const override {
    return true;
  }

 private:
  // --- The parser will decorate this node with the following information --

  PTExprListNode::SharedPtr target_;
  PTTableRef::SharedPtr relation_;
};

}  // namespace ql
}  // namespace yb
